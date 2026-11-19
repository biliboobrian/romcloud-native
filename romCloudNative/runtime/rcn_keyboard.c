// RomCloud Native : clavier de l'hôte (clavier virtuel d'Android, ou clavier physique) vu par le jeu.
//
// L'application envoie du texte (rcn_keyboard_text) et des touches spéciales (rcn_keyboard_key) ; ils
// deviennent des frappes d'un clavier QWERTY américain (codes de balayage de DirectInput, touches
// virtuelles de Windows), majuscule enfoncée au besoin. Les caractères absents de ce clavier (accents…)
// passent par des touches libres dont ToUnicode rend le caractère.
//
// Les frappes vont au clavier DirectInput si le jeu en a créé un (rcn_dinput8.c), sinon à sa fenêtre
// (WM_KEYDOWN, WM_CHAR, WM_KEYUP : rcn_user32_x64.c) : jamais aux deux, un caractère serait tapé deux fois.
#include <pthread.h>

#include "rcn_internal.h"

enum { QUEUE = 512, VK_SHIFT = 0x10, VK_LSHIFT = 0xA0, VK_CAPITAL = 0x14, SCAN_LSHIFT = 0x2A, FREE_FIRST = 0x70, FREE_COUNT = 16 };

typedef struct {
    u8 scan, vk, down;
    u16 ch; // caractère tapé (touche enfoncée), 0 sinon
    u32 time;
} RcnKeyEvent;

// Touches du clavier QWERTY américain : code de balayage, touche virtuelle, caractère sans et avec majuscule.
static const struct {
    u8 scan, vk;
    char lower, upper;
} keys[] = {
    {0x02, '1', '1', '!'}, {0x03, '2', '2', '@'}, {0x04, '3', '3', '#'}, {0x05, '4', '4', '$'}, {0x06, '5', '5', '%'},
    {0x07, '6', '6', '^'}, {0x08, '7', '7', '&'}, {0x09, '8', '8', '*'}, {0x0A, '9', '9', '('}, {0x0B, '0', '0', ')'},
    {0x0C, 0xBD, '-', '_'}, {0x0D, 0xBB, '=', '+'}, {0x10, 'Q', 'q', 'Q'}, {0x11, 'W', 'w', 'W'}, {0x12, 'E', 'e', 'E'},
    {0x13, 'R', 'r', 'R'}, {0x14, 'T', 't', 'T'}, {0x15, 'Y', 'y', 'Y'}, {0x16, 'U', 'u', 'U'}, {0x17, 'I', 'i', 'I'},
    {0x18, 'O', 'o', 'O'}, {0x19, 'P', 'p', 'P'}, {0x1A, 0xDB, '[', '{'}, {0x1B, 0xDD, ']', '}'}, {0x1E, 'A', 'a', 'A'},
    {0x1F, 'S', 's', 'S'}, {0x20, 'D', 'd', 'D'}, {0x21, 'F', 'f', 'F'}, {0x22, 'G', 'g', 'G'}, {0x23, 'H', 'h', 'H'},
    {0x24, 'J', 'j', 'J'}, {0x25, 'K', 'k', 'K'}, {0x26, 'L', 'l', 'L'}, {0x27, 0xBA, ';', ':'}, {0x28, 0xDE, '\'', '"'},
    {0x29, 0xC0, '`', '~'}, {0x2B, 0xDC, '\\', '|'}, {0x2C, 'Z', 'z', 'Z'}, {0x2D, 'X', 'x', 'X'}, {0x2E, 'C', 'c', 'C'},
    {0x2F, 'V', 'v', 'V'}, {0x30, 'B', 'b', 'B'}, {0x31, 'N', 'n', 'N'}, {0x32, 'M', 'm', 'M'}, {0x33, 0xBC, ',', '<'},
    {0x34, 0xBE, '.', '>'}, {0x35, 0xBF, '/', '?'}, {0x39, 0x20, ' ', ' '},
};
// Touches sans caractère : code de balayage (0x80 : étendu, préfixe E0), touche virtuelle.
static const struct {
    u8 scan, vk;
} specials[] = {
    {0x01, 0x1B}, {0x0E, 0x08}, {0x0F, 0x09}, {0x1C, 0x0D}, {0x1D, 0x11}, {0x2A, 0x10}, {0x36, 0x10}, {0x38, 0x12},
    {0x3A, 0x14}, {0xC8, 0x26}, {0xCB, 0x25}, {0xCD, 0x27}, {0xD0, 0x28}, {0xC7, 0x24}, {0xCF, 0x23}, {0xD3, 0x2E},
    {0xC9, 0x21}, {0xD1, 0x22}, {0x3B, 0x70}, {0x3C, 0x71}, {0x3D, 0x72}, {0x3E, 0x73}, {0x3F, 0x74}, {0x40, 0x75},
    {0x41, 0x76}, {0x42, 0x77}, {0x43, 0x78}, {0x44, 0x79}, {0x57, 0x7A}, {0x58, 0x7B},
};

static struct {
    pthread_mutex_t lock;
    RcnKeyEvent queue[QUEUE];
    u32 written; // nombre d'événements écrits depuis le début
    u8 scan_down[256], vk_down[256];
    u16 free_char[FREE_COUNT]; // caractère des touches libres FREE_FIRST…
    u32 free_next;
    int dinput_keyboard; // un clavier DirectInput a été créé
    void (*want_text)(void *user, int on);
    void *want_text_user;
} kb = {.lock = PTHREAD_MUTEX_INITIALIZER};

static u32 now_ms(void) { return (u32)(rcn_now_ns() / 1000000); }

static u8 vk_of_scan(u8 scan) {
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (keys[i].scan == scan) return keys[i].vk;
    for (size_t i = 0; i < sizeof specials / sizeof specials[0]; i++)
        if (specials[i].scan == scan) return specials[i].vk;
    if (scan >= FREE_FIRST && scan < FREE_FIRST + FREE_COUNT) return 0xE5; // VK_PROCESSKEY : caractère de la touche libre
    return 0;
}
static u8 scan_of_vk(u8 vk) {
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (keys[i].vk == vk) return keys[i].scan;
    for (size_t i = 0; i < sizeof specials / sizeof specials[0]; i++)
        if (specials[i].vk == vk) return specials[i].scan;
    return 0;
}

// Sous le verrou.
static void push(u8 scan, u8 vk, int down, u16 ch) {
    RcnKeyEvent *e = &kb.queue[kb.written % QUEUE];
    *e = (RcnKeyEvent){scan, vk, (u8)(down != 0), down ? ch : 0, now_ms()};
    kb.written++;
    kb.scan_down[scan] = (u8)(down != 0);
    kb.vk_down[vk] = (u8)(down != 0);
    if (vk == VK_SHIFT) kb.vk_down[VK_LSHIFT] = (u8)(down != 0);
}

// Texte tapé (UTF-16) : une frappe par caractère.
void rcn_keyboard_text(const u16 *text, int n) {
    pthread_mutex_lock(&kb.lock);
    for (int i = 0; i < n; i++) {
        u16 c = text[i];
        if (c == '\n' || c == '\r') {
            push(0x1C, 0x0D, 1, '\r');
            push(0x1C, 0x0D, 0, 0);
            continue;
        }
        int found = -1, shift = 0;
        for (size_t k = 0; c < 128 && k < sizeof keys / sizeof keys[0] && found < 0; k++) {
            if ((u8)keys[k].lower == c) found = (int)k;
            else if ((u8)keys[k].upper == c) found = (int)k, shift = 1;
        }
        if (found >= 0) {
            if (shift) push(SCAN_LSHIFT, VK_SHIFT, 1, 0);
            push(keys[found].scan, keys[found].vk, 1, c);
            push(keys[found].scan, keys[found].vk, 0, 0);
            if (shift) push(SCAN_LSHIFT, VK_SHIFT, 0, 0);
        } else {
            // Caractère hors du clavier américain : touche libre suivante, dont ToUnicode rendra le caractère.
            u8 slot = (u8)(kb.free_next++ % FREE_COUNT);
            kb.free_char[slot] = c;
            push((u8)(FREE_FIRST + slot), 0xE5, 1, c);
            push((u8)(FREE_FIRST + slot), 0xE5, 0, 0);
        }
    }
    pthread_mutex_unlock(&kb.lock);
}

// Touche spéciale (touche virtuelle de Windows : VK_BACK, VK_RETURN, VK_ESCAPE, flèches…), enfoncée ou relâchée.
void rcn_keyboard_key(int vk, int down) {
    u8 scan = scan_of_vk((u8)vk);
    if (!scan) return;
    u16 ch = vk == 0x08 ? 8 : vk == 0x0D ? '\r' : vk == 0x09 ? '\t' : vk == 0x1B ? 27 : 0;
    pthread_mutex_lock(&kb.lock);
    push(scan, (u8)vk, down, ch);
    pthread_mutex_unlock(&kb.lock);
}

// Événements écrits depuis *cursor (au plus max), cursor avancé. Un lecteur trop lent perd les plus anciens.
static u32 read_events(u32 *cursor, RcnKeyEvent *out, u32 max) {
    pthread_mutex_lock(&kb.lock);
    if (kb.written - *cursor > QUEUE) *cursor = kb.written - QUEUE;
    u32 n = 0;
    while (*cursor != kb.written && n < max) out[n++] = kb.queue[(*cursor)++ % QUEUE];
    pthread_mutex_unlock(&kb.lock);
    return n;
}

// --- DirectInput (rcn_dinput8.c) ---

// Clavier DirectInput créé : il reçoit les frappes (la fenêtre, plus). Rend le curseur de départ.
u32 rcn_keyboard_dinput_open(void) {
    pthread_mutex_lock(&kb.lock);
    kb.dinput_keyboard = 1;
    u32 cursor = kb.written;
    pthread_mutex_unlock(&kb.lock);
    return cursor;
}
// GetDeviceData : DIDEVICEOBJECTDATA de 64 bits (24 octets : touche, donnée, temps, séquence, appli).
u32 rcn_keyboard_dinput_read(u32 *cursor, u64 out, u32 size, u32 max) {
    RcnKeyEvent e[64];
    u32 n = read_events(cursor, e, max < 64 ? max : 64);
    for (u32 i = 0; i < n; i++) {
        u64 at = out + (u64)i * size;
        memset(PTR(at), 0, size);
        wr32(at, e[i].scan);
        wr32(at + 4, e[i].down ? 0x80 : 0);
        wr32(at + 8, e[i].time);
        wr32(at + 12, *cursor - n + i);
    }
    return n;
}
// GetDeviceState : 256 octets, 0x80 par touche enfoncée.
void rcn_keyboard_dinput_state(u8 *out) {
    pthread_mutex_lock(&kb.lock);
    for (int i = 0; i < 256; i++) out[i] = kb.scan_down[i] ? 0x80 : 0;
    pthread_mutex_unlock(&kb.lock);
}

// --- Fenêtre (rcn_user32_x64.c) : frappes à remettre en messages, si aucun clavier DirectInput ---

u32 rcn_keyboard_window_read(u32 *cursor, u8 *scan, u8 *vk, u8 *down, u16 *ch, u32 max) {
    RcnKeyEvent e[64];
    if (kb.dinput_keyboard) {
        pthread_mutex_lock(&kb.lock);
        *cursor = kb.written;
        pthread_mutex_unlock(&kb.lock);
        return 0;
    }
    u32 n = read_events(cursor, e, max < 64 ? max : 64);
    for (u32 i = 0; i < n; i++) scan[i] = e[i].scan, vk[i] = e[i].vk, down[i] = e[i].down, ch[i] = e[i].ch;
    return n;
}

// --- user32 : MapVirtualKey, ToUnicode, GetKeyState ---

// MapVirtualKeyA / W(code, type) : 0 touche → balayage, 1 et 3 balayage → touche, 2 touche → caractère.
// Codes de balayage : ceux des tables sont ceux de DirectInput (0x80 : touche étendue) ; Windows les écrit
// sans ce bit (MAPVK_VK_TO_VSC) ou avec le préfixe 0xE0 (…_EX : 0xE048 pour la flèche du haut).
// Touches gauche / droite des modificateurs : VK_LSHIFT… (0xA0-0xA5) ↔ balayage (étendu pour Ctrl et Alt droits).
static const struct {
    u8 vk, generic;
    u16 scan; // préfixe 0xE0 compris
} sided[] = {{0xA0, 0x10, 0x2A}, {0xA1, 0x10, 0x36}, {0xA2, 0x11, 0x1D}, {0xA3, 0x11, 0xE01D}, {0xA4, 0x12, 0x38}, {0xA5, 0x12, 0xE038}};
static u32 vk_to_scan_ex(u8 vk) {
    for (size_t i = 0; i < sizeof sided / sizeof sided[0]; i++)
        if (sided[i].vk == vk) return sided[i].scan;
    u8 dik = scan_of_vk(vk);
    return dik & 0x80 ? 0xE000u | (dik & 0x7F) : dik;
}
// MapVirtualKey(Ex)A / W(code, type[, disposition]) : 0 touche → balayage, 1 balayage → touche, 2 touche →
// caractère, 3 balayage (préfixe 0xE0 admis) → touche gauche / droite, 4 touche → balayage avec préfixe 0xE0.
// Unreal Engine 5 ramène chaque touche à sa touche physique (touche → balayage → touche, disposition
// américaine) : à 0, toute frappe du clavier était ignorée (Clair Obscur : Entrée sans effet).
static void k_MapVirtualKey(RcnCtx *ctx) {
    u32 code = (u32)A0, type = (u32)A1;
    switch (type) {
    case 0: RET(vk_to_scan_ex((u8)code) & 0xFF); break;
    case 4: RET(code < 256 ? vk_to_scan_ex((u8)code) : 0); break;
    case 1:
    case 3: {
        int extended = (code & 0xFF00) == 0xE000 || (code & 0xFF00) == 0xE100;
        u8 low = (u8)code;
        if ((code & 0xFF00) && !extended) {
            RET(0);
            break;
        }
        u32 vk = 0;
        for (size_t i = 0; i < sizeof sided / sizeof sided[0] && !vk; i++)
            if (sided[i].scan == (extended ? 0xE000u | low : low)) vk = type == 3 ? sided[i].vk : sided[i].generic;
        if (!vk && extended) vk = vk_of_scan((u8)(0x80 | low));
        if (!vk) vk = vk_of_scan(low);
        // MAPVK_VSC_TO_VK sans préfixe : les touches de navigation partagent leur code avec le pavé numérique.
        if (!vk && !extended && type == 1) vk = vk_of_scan((u8)(0x80 | low));
        RET(vk);
        break;
    }
    case 2: {
        u8 vk = (u8)code;
        u32 c = 0;
        if (vk == 0x08 || vk == 0x09 || vk == 0x0D || vk == 0x1B) c = vk; // retour arrière, tabulation, entrée, échap
        u8 scan = scan_of_vk(vk);
        for (size_t i = 0; i < sizeof keys / sizeof keys[0] && !c; i++)
            if (keys[i].scan == scan) c = (u8)(keys[i].lower >= 'a' && keys[i].lower <= 'z' ? keys[i].upper : keys[i].lower);
        RET(c);
        break;
    }
    default: RET(0);
    }
}
// ToUnicode(touche, balayage, état[256], tampon, taille, options) : caractère de la frappe.
static void k_ToUnicode(RcnCtx *ctx) {
    u32 vk = (u32)A0, scan = (u32)A1 & 0xFF;
    u64 state = A2, out = A3;
    i32 size = (i32)rcn_arg(ctx, 4);
    if (!out || size < 1) {
        RET(0);
        return;
    }
    u16 c = 0;
    if (scan >= FREE_FIRST && scan < FREE_FIRST + FREE_COUNT) {
        pthread_mutex_lock(&kb.lock);
        c = kb.free_char[scan - FREE_FIRST];
        pthread_mutex_unlock(&kb.lock);
    } else {
        // Majuscule : celle de l'état donné, ou à défaut celle du clavier.
        int shift = state ? ((rd8(state + VK_SHIFT) | rd8(state + VK_LSHIFT) | rd8(state + 0xA1)) & 0x80) != 0 : 0;
        if (!shift) shift = kb.vk_down[VK_SHIFT];
        int caps = state ? (rd8(state + VK_CAPITAL) & 1) : 0;
        if (!scan) scan = scan_of_vk((u8)vk);
        for (size_t i = 0; i < sizeof keys / sizeof keys[0] && !c; i++) {
            if (keys[i].scan != scan) continue;
            int upper = shift;
            if (keys[i].lower >= 'a' && keys[i].lower <= 'z' && caps) upper = !upper;
            c = (u8)(upper ? keys[i].upper : keys[i].lower);
        }
        if (!c && (vk == 0x0D || vk == 0x08 || vk == 0x09 || vk == 0x1B)) c = vk == 0x0D ? '\r' : (u16)vk;
    }
    if (!c) {
        RET(0);
        return;
    }
    wr16(out, c);
    if (size > 1) wr16(out + 2, 0);
    RET(1);
}
static void k_GetKeyState(RcnCtx *ctx) {
    u32 vk = (u32)A0 & 0xFF;
    RET(kb.vk_down[vk] ? 0xFF80 : 0); // SHORT négatif : enfoncée
}
static void k_GetAsyncKeyState(RcnCtx *ctx) {
    u32 vk = (u32)A0 & 0xFF;
    RET(kb.vk_down[vk] ? 0x8000 : 0);
}
// GetKeyboardState(état[256])
static void k_GetKeyboardState(RcnCtx *ctx) {
    if (!A0) {
        RET(0);
        return;
    }
    for (int i = 0; i < 256; i++) wr8(A0 + (u64)i, kb.vk_down[i] ? 0x80 : 0);
    RET(1);
}

const RcnShim rcn_keyboard_user32[] = {
    {"MapVirtualKeyA", k_MapVirtualKey, 0}, {"MapVirtualKeyW", k_MapVirtualKey, S32(2)},
    {"MapVirtualKeyExA", k_MapVirtualKey, 0}, {"MapVirtualKeyExW", k_MapVirtualKey, S32(3)},
    {"ToUnicode", k_ToUnicode, S32(6)}, {"ToUnicodeEx", k_ToUnicode, 0},
    {"GetKeyState", k_GetKeyState, S32(1)}, {"GetAsyncKeyState", k_GetAsyncKeyState, S32(1)},
    {"GetKeyboardState", k_GetKeyboardState, S32(1)},
    {NULL, NULL, 0},
};

// --- Saisie de texte attendue (SDL_StartTextInput…) : l'hôte montre son clavier ---

void rcn_keyboard_set_host(void (*want_text)(void *user, int on), void *user) {
    kb.want_text = want_text;
    kb.want_text_user = user;
}
void rcn_keyboard_want_text(int on) {
    if (kb.want_text) kb.want_text(kb.want_text_user, on);
}
