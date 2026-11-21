// RomCloud Native : user32 des jeux 64 bits qui ont leur propre fenêtre Win32 (Unity, moteurs maison).
//
// Les fenêtres n'existent que pour le jeu : leurs procédures (le code du jeu) reçoivent les messages
// habituels (création, taille, activation, fermeture), la file de messages sert PeekMessage /
// GetMessage / DispatchMessage, les minuteurs y déposent WM_TIMER. L'écran est unique, à la taille
// de celui de l'hôte (1920 × 1080 annoncés). Clavier, souris et périphériques HID : absents (la manette
// passe par XInput).
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_gui.h"

enum {
    WM_CREATE = 0x01, WM_DESTROY = 0x02, WM_MOVE = 0x03, WM_SIZE = 0x05, WM_ACTIVATE = 0x06, WM_SETFOCUS = 0x07,
    WM_CLOSE = 0x10, WM_QUIT = 0x12, WM_SHOWWINDOW = 0x18, WM_ACTIVATEAPP = 0x1C, WM_WINDOWPOSCHANGED = 0x47,
    WM_NCCREATE = 0x81, WM_NCDESTROY = 0x82, WM_NCHITTEST = 0x84, WM_TIMER = 0x113,
    PM_REMOVE = 1, CW_USEDEFAULT = (int)0x80000000,
    SWP_NOSIZE = 1, SWP_NOMOVE = 2, SWP_SHOWWINDOW = 0x40, SWP_HIDEWINDOW = 0x80,
    MAX_WINDOWS = 32, MAX_CLASSES = 32, QUEUE_SIZE = 512, MAX_TIMERS = 64,
    MONITOR = 0x30001, DESKTOP = 0x10010, FAKE_DC = 0x30010, FAKE_ICON = 0x40001,
};

// Taille de l'écran imité : 1920 × 1080, ou RCN_SCREEN=<largeur>x<hauteur> (résolution choisie pour le jeu
// dans l'application : le jeu ne peut pas en prendre une plus grande, moins de pixels pour le processeur
// graphique ; l'application agrandit l'image à l'écran).
int rcn_screen_size(int height) {
    static int w, h;
    if (!w) {
        const char *v = getenv("RCN_SCREEN");
        int x = 0, y = 0;
        if (v && sscanf(v, "%dx%d", &x, &y) == 2 && x >= 320 && y >= 200 && x <= 7680 && y <= 4320) h = y, w = x;
        else h = 1080, w = 1920;
    }
    return height ? h : w;
}
#define SCREEN_W rcn_screen_size(0)
#define SCREEN_H rcn_screen_size(1)

typedef struct {
    int used;
    u64 hwnd, wndproc, parent, userdata, id, instance;
    u32 style, exstyle;
    i32 x, y, w, h;
    int visible, shown_once;
    u8 extra[256];
    u32 extra_size;
} Window;
typedef struct {
    char name[128];
    u16 atom;
    u64 wndproc;
    u32 extra;
} Class;
typedef struct {
    u64 hwnd;
    u32 message;
    u64 w, l;
} Msg;
typedef struct {
    u64 hwnd, id, proc;
    u32 period;
    u64 due_ns;
} Timer;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    Window windows[MAX_WINDOWS];
    Class classes[MAX_CLASSES];
    u32 class_count;
    Msg queue[QUEUE_SIZE];
    u32 head, count;
    Timer timers[MAX_TIMERS];
    u64 focus, main;
    int quit_posted, cursor_count;
} us = {.lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};

static char *utf8(u64 text) { return text ? rcn_utf16_to_utf8(PTR(text), -1) : NULL; }

static Window *window_of(u64 hwnd) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (us.windows[i].used && us.windows[i].hwnd == hwnd) return &us.windows[i];
    }
    return NULL;
}

// Appel de la procédure d'une fenêtre (code du jeu).
static u64 call_proc(RcnCtx *ctx, u64 hwnd, u32 message, u64 w, u64 l) {
    Window *win = window_of(hwnd);
    if (!win || !win->wndproc) return 0;
    return rcn_call_guest(ctx, win->wndproc, hwnd, message, w, l);
}

static void post(u64 hwnd, u32 message, u64 w, u64 l) {
    pthread_mutex_lock(&us.lock);
    if (us.count < QUEUE_SIZE) {
        Msg *m = &us.queue[(us.head + us.count++) % QUEUE_SIZE];
        m->hwnd = hwnd;
        m->message = message;
        m->w = w;
        m->l = l;
        pthread_cond_broadcast(&us.cond);
    }
    pthread_mutex_unlock(&us.lock);
}

// Minuteurs échus : WM_TIMER en file (verrou tenu).
static void fire_timers_locked(void) {
    u64 now = rcn_now_ns();
    for (int i = 0; i < MAX_TIMERS; i++) {
        Timer *t = &us.timers[i];
        if (!t->period || now < t->due_ns || us.count >= QUEUE_SIZE) continue;
        Msg *m = &us.queue[(us.head + us.count++) % QUEUE_SIZE];
        m->hwnd = t->hwnd;
        m->message = WM_TIMER;
        m->w = t->id;
        m->l = t->proc;
        t->due_ns = now + (u64)t->period * 1000000ull;
    }
}

static int matches(const Msg *m, u64 hwnd, u32 min, u32 max) {
    if (hwnd == (u64)-1 && m->hwnd) return 0;
    if (hwnd && hwnd != (u64)-1 && m->hwnd != hwnd) return 0;
    if ((min || max) && (m->message < min || m->message > max)) return 0;
    return 1;
}

// Premier message de la file qui convient, retiré si `remove`. 0 s'il n'y en a pas.
u32 rcn_keyboard_window_read(u32 *cursor, u8 *scan, u8 *vk, u8 *down, u16 *ch, u32 max);
// Frappes du clavier de l'hôte (rcn_keyboard.c) : WM_KEYDOWN, WM_CHAR, WM_KEYUP à la fenêtre active.
static void post_keys(void) {
    static u32 cursor;
    static int log_keys = -1;
    if (log_keys < 0) log_keys = getenv("RCN_KEY_LOG") != NULL;
    u8 scan[32], vk[32], down[32];
    u16 ch[32];
    u64 target = us.focus ? us.focus : us.main;
    if (!target) return;
    u32 n = rcn_keyboard_window_read(&cursor, scan, vk, down, ch, 32);
    for (u32 i = 0; i < n; i++) {
        u64 l = 1 | ((u64)(scan[i] & 0x7F) << 16) | (scan[i] & 0x80 ? 1ull << 24 : 0);
        if (down[i]) {
            post(target, 0x0100, vk[i], l); // WM_KEYDOWN
            if (ch[i]) {
                post(target, 0x0102, ch[i], l);    // WM_CHAR
                post(target, 0x0286, ch[i], l);    // WM_IME_CHAR : certains moteurs ne lisent que celui-ci
            }
            if (log_keys && ch[i]) rcn_log("clavier : WM_CHAR 0x%02x a la fenetre %llx", ch[i], (unsigned long long)target);
        } else {
            post(target, 0x0101, vk[i], l | 0xC0000000ull); // WM_KEYUP
        }
    }
}

static int take(RcnCtx *ctx, u64 out, u64 hwnd, u32 min, u32 max, int remove) {
    // Fermeture demandée par l'utilisateur : WM_CLOSE à la fenêtre principale.
    if (!us.quit_posted && us.main && (rcn_pad_buttons(ctx) & RCN_PAD_QUIT)) {
        us.quit_posted = 1;
        post(us.main, WM_CLOSE, 0, 0);
    }
    post_keys();
    pthread_mutex_lock(&us.lock);
    fire_timers_locked();
    int found = 0;
    for (u32 k = 0; k < us.count; k++) {
        u32 at = (us.head + k) % QUEUE_SIZE;
        Msg m = us.queue[at];
        if (!matches(&m, hwnd, min, max)) continue;
        found = 1;
        if (out && ctx->bits32) {
            // MSG de 32 bits (28 octets) : hwnd, message, wParam, lParam, time, pt.
            memset(PTR(out), 0, 28);
            wr32(out, (u32)m.hwnd);
            wr32(out + 4, m.message);
            wr32(out + 8, (u32)m.w);
            wr32(out + 12, (u32)m.l);
            wr32(out + 16, (u32)(rcn_now_ns() / 1000000));
        } else if (out) {
            memset(PTR(out), 0, 48);
            wr64(out, m.hwnd);
            wr32(out + 8, m.message);
            wr64(out + 16, m.w);
            wr64(out + 24, m.l);
            wr32(out + 32, (u32)(rcn_now_ns() / 1000000));
        }
        if (remove) {
            for (u32 j = k; j + 1 < us.count; j++) us.queue[(us.head + j) % QUEUE_SIZE] = us.queue[(us.head + j + 1) % QUEUE_SIZE];
            us.count--;
        }
        break;
    }
    pthread_mutex_unlock(&us.lock);
    return found;
}

// --- Classes et fenêtres ---

static u16 add_class(RcnCtx *ctx, u64 name, u64 wndproc, u32 extra) {
    if (us.class_count >= MAX_CLASSES) return 0;
    Class *c = &us.classes[us.class_count];
    c->atom = (u16)(0xC000 + us.class_count);
    c->wndproc = wndproc;
    c->extra = extra;
    if (name >= 0x10000) {
        char *s = utf8(name);
        snprintf(c->name, sizeof c->name, "%s", s ? s : "");
        free(s);
    } else {
        snprintf(c->name, sizeof c->name, "#%u", (u32)name);
    }
    (void)ctx;
    us.class_count++;
    return c->atom;
}
static Class *class_of(u64 name) {
    char key[128];
    if (name < 0x10000) {
        for (u32 i = 0; i < us.class_count; i++) {
            if (us.classes[i].atom == (u16)name) return &us.classes[i];
        }
        snprintf(key, sizeof key, "#%u", (u32)name);
    } else {
        char *s = utf8(name);
        snprintf(key, sizeof key, "%s", s ? s : "");
        free(s);
    }
    for (u32 i = 0; i < us.class_count; i++) {
        if (strcasecmp(us.classes[i].name, key) == 0) return &us.classes[i];
    }
    return NULL;
}
// RegisterClassExW(&wndclassex) / RegisterClassW(&wndclass)
// 32 bits : WNDCLASSEXW (lpfnWndProc +8, cbWndExtra +16, lpszClassName +40), WNDCLASSW (+4, +12, +36).
static void u_RegisterClassExW(RcnCtx *ctx) {
    if (ctx->bits32) RET(add_class(ctx, rd32(A0 + 40), rd32(A0 + 8), rd32(A0 + 16)));
    else RET(add_class(ctx, rd64(A0 + 64), rd64(A0 + 8), rd32(A0 + 20)));
}
static void u_RegisterClassW(RcnCtx *ctx) {
    if (ctx->bits32) RET(add_class(ctx, rd32(A0 + 36), rd32(A0 + 4), rd32(A0 + 12)));
    else RET(add_class(ctx, rd64(A0 + 64), rd64(A0 + 8), rd32(A0 + 20)));
}
static void u_UnregisterClassW(RcnCtx *ctx) { RET(1); }

static void send_move(RcnCtx *ctx, Window *win);
static void send_size(RcnCtx *ctx, Window *win) {
    u64 hwnd = win->hwnd;
    call_proc(ctx, hwnd, WM_SIZE, 0, ((u64)(u16)win->h << 16) | (u16)win->w);
}

// CreateWindowExW(ex, class, title, style, x, y, w, h, parent, menu, instance, param)
static void u_CreateWindowExW(RcnCtx *ctx) {
    Class *c = class_of(A1);
    Window *win = NULL;
    pthread_mutex_lock(&us.lock);
    for (int i = 0; i < MAX_WINDOWS && !win; i++) {
        if (!us.windows[i].used) {
            win = &us.windows[i];
            memset(win, 0, sizeof *win);
            win->used = 1;
            win->hwnd = 0x20002 + 2 * (u64)i;
        }
    }
    pthread_mutex_unlock(&us.lock);
    if (!win) {
        RET(0);
        return;
    }
    win->exstyle = (u32)A0;
    win->style = (u32)A3;
    win->x = (i32)STK(4) == CW_USEDEFAULT ? 0 : (i32)STK(4);
    win->y = (i32)STK(5) == CW_USEDEFAULT ? 0 : (i32)STK(5);
    win->w = (i32)STK(6) == CW_USEDEFAULT || (i32)STK(6) <= 0 ? 1280 : (i32)STK(6);
    win->h = (i32)STK(7) == CW_USEDEFAULT || (i32)STK(7) <= 0 ? 720 : (i32)STK(7);
    win->parent = STK(8);
    win->id = STK(9);
    win->instance = STK(10);
    win->wndproc = c ? c->wndproc : 0;
    win->extra_size = c && c->extra < sizeof win->extra ? c->extra : 0;
    // Première fenêtre de premier niveau (ni fille, ni fenêtre de messages) : la fenêtre principale.
    if (!us.main && (!win->parent || win->parent == DESKTOP)) us.main = win->hwnd;
    // CREATESTRUCTW pour WM_NCCREATE et WM_CREATE.
    // Dans la mémoire du jeu (adresse de 32 bits pour un jeu 32 bits).
    u8 *cs = rcn_galloc(ctx, 80, 1);
    if (cs && ctx->bits32) {
        // CREATESTRUCTW de 32 bits (48 octets).
        u64 p = (u64)(uintptr_t)cs;
        wr32(p, (u32)STK(11)), wr32(p + 4, (u32)win->instance), wr32(p + 8, (u32)win->id), wr32(p + 12, (u32)win->parent);
        wr32(p + 16, (u32)win->h), wr32(p + 20, (u32)win->w), wr32(p + 24, (u32)win->y), wr32(p + 28, (u32)win->x);
        wr32(p + 32, win->style), wr32(p + 36, (u32)A2), wr32(p + 40, (u32)A1), wr32(p + 44, win->exstyle);
    } else if (cs) {
        u64 p = (u64)(uintptr_t)cs;
        wr64(p, STK(11));
        wr64(p + 8, win->instance);
        wr64(p + 16, win->id);
        wr64(p + 24, win->parent);
        wr32(p + 32, (u32)win->h);
        wr32(p + 36, (u32)win->w);
        wr32(p + 40, (u32)win->y);
        wr32(p + 44, (u32)win->x);
        wr32(p + 48, win->style);
        wr64(p + 56, A2);
        wr64(p + 64, A1);
        wr32(p + 72, win->exstyle);
    }
    if (cs) {
        u64 p = (u64)(uintptr_t)cs;
        u64 hwnd = win->hwnd;
        if (!call_proc(ctx, hwnd, WM_NCCREATE, 0, p) || (i64)call_proc(ctx, hwnd, WM_CREATE, 0, p) == -1) {
            win->used = 0;
            if (us.main == hwnd) us.main = 0;
            rcn_gfree(ctx, cs);
            RET(0);
            return;
        }
        rcn_gfree(ctx, cs);
    }
    if (win->style & 0x10000000) { // WS_VISIBLE : la fenêtre paraît aussitôt (Windows envoie alors les messages d'activation)
        win->visible = 1;
        win->shown_once = 1;
        us.focus = win->hwnd;
        call_proc(ctx, win->hwnd, WM_SHOWWINDOW, 1, 0);
        call_proc(ctx, win->hwnd, WM_ACTIVATEAPP, 1, 0);
        call_proc(ctx, win->hwnd, WM_ACTIVATE, 1, 0);
        call_proc(ctx, win->hwnd, WM_SETFOCUS, 0, 0);
        send_size(ctx, win);
        send_move(ctx, win);
    }
    if (ctx->proc->trace) fprintf(stderr, " [fenetre %llx %dx%d]", (unsigned long long)win->hwnd, win->w, win->h);
    RET(win->hwnd);
}
static void u_DestroyWindow(RcnCtx *ctx) {
    u64 hwnd = A0;
    Window *win = window_of(hwnd);
    if (!win) {
        RET(0);
        return;
    }
    call_proc(ctx, hwnd, WM_DESTROY, 0, 0);
    call_proc(ctx, hwnd, WM_NCDESTROY, 0, 0);
    win->used = 0;
    pthread_mutex_lock(&us.lock);
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (us.timers[i].hwnd == hwnd) us.timers[i].period = 0;
    }
    pthread_mutex_unlock(&us.lock);
    RET(1);
}
// DefWindowProcW(hwnd, message, w, l)
static void u_DefWindowProcW(RcnCtx *ctx) {
    switch ((u32)A1) {
    case WM_NCCREATE: RET(1); return;
    case WM_CLOSE: {
        u64 hwnd = A0;
        Window *win = window_of(hwnd);
        if (win) {
            call_proc(ctx, hwnd, WM_DESTROY, 0, 0);
            win->used = 0;
        }
        RET(0);
        return;
    }
    case WM_NCHITTEST: RET(1); return; // HTCLIENT
    default: RET(0); return;
    }
}

static void u_ShowWindow(RcnCtx *ctx) {
    u64 hwnd = A0;
    Window *win = window_of(hwnd);
    if (!win) {
        RET(0);
        return;
    }
    int previous = win->visible;
    win->visible = (u32)A1 != 0; // SW_HIDE 0
    if (win->visible && !win->shown_once) {
        win->shown_once = 1;
        us.focus = hwnd;
        call_proc(ctx, hwnd, WM_SHOWWINDOW, 1, 0);
        call_proc(ctx, hwnd, WM_ACTIVATEAPP, 1, 0);
        call_proc(ctx, hwnd, WM_ACTIVATE, 1, 0);
        call_proc(ctx, hwnd, WM_SETFOCUS, 0, 0);
        send_size(ctx, win);
        send_move(ctx, win);
    }
    RET(previous);
}
// WM_MOVE(0, position de la zone cliente à l'écran) : Unreal Engine (Slate) y apprend où est sa fenêtre ;
// sans lui, il ne trouvait aucun widget sous le pointeur (survol, clic et validation à la manette perdus).
static void send_move(RcnCtx *ctx, Window *win) {
    call_proc(ctx, win->hwnd, WM_MOVE, 0, ((u64)(u16)(i16)win->y << 16) | (u16)(i16)win->x);
}
static void resize(RcnCtx *ctx, Window *win, i32 x, i32 y, i32 w, i32 h, int move, int size) {
    if (move && (x != win->x || y != win->y)) {
        win->x = x;
        win->y = y;
        send_move(ctx, win);
    }
    if (size && w > 0 && h > 0 && (w != win->w || h != win->h)) {
        win->w = w;
        win->h = h;
        send_size(ctx, win);
    }
}
// SetWindowPos(hwnd, after, x, y, cx, cy, flags)
static void u_SetWindowPos(RcnCtx *ctx) {
    Window *win = window_of(A0);
    if (!win) {
        RET(0);
        return;
    }
    u32 flags = (u32)STK(6);
    resize(ctx, win, (i32)A2, (i32)A3, (i32)STK(4), (i32)STK(5), !(flags & SWP_NOMOVE), !(flags & SWP_NOSIZE));
    if (flags & SWP_SHOWWINDOW) win->visible = 1;
    if (flags & SWP_HIDEWINDOW) win->visible = 0;
    RET(1);
}
static void u_MoveWindow(RcnCtx *ctx) { // (hwnd, x, y, w, h, repaint)
    Window *win = window_of(A0);
    if (win) resize(ctx, win, (i32)A1, (i32)A2, (i32)A3, (i32)STK(4), 1, 1);
    RET(win != NULL);
}
static void write_rect(u64 r, i32 l, i32 t, i32 rr, i32 b) {
    wr32(r, (u32)l);
    wr32(r + 4, (u32)t);
    wr32(r + 8, (u32)rr);
    wr32(r + 12, (u32)b);
}
static void u_GetClientRect(RcnCtx *ctx) {
    Window *win = window_of(A0);
    if (win) write_rect(A1, 0, 0, win->w, win->h);
    else write_rect(A1, 0, 0, SCREEN_W, SCREEN_H);
    RET(1);
}
static void u_GetWindowRect(RcnCtx *ctx) {
    Window *win = window_of(A0);
    if (win) write_rect(A1, win->x, win->y, win->x + win->w, win->y + win->h);
    else write_rect(A1, 0, 0, SCREEN_W, SCREEN_H);
    RET(1);
}
static void u_AdjustWindowRectEx(RcnCtx *ctx) { RET(1); } // pas de bords
static void point_shift(RcnCtx *ctx, int sign) {
    Window *win = window_of(A0);
    if (win) {
        wr32(A1, (u32)((i32)rd32(A1) + sign * win->x));
        wr32(A1 + 4, (u32)((i32)rd32(A1 + 4) + sign * win->y));
    }
    RET(1);
}
static void u_ClientToScreen(RcnCtx *ctx) { point_shift(ctx, 1); }
static void u_ScreenToClient(RcnCtx *ctx) { point_shift(ctx, -1); }

// Champs d'une fenêtre (GWLP_*), et octets supplémentaires de sa classe (indices positifs).
static u64 *long_slot(Window *win, i32 index) {
    switch (index) {
    case -4: return &win->wndproc;   // GWLP_WNDPROC
    case -6: return &win->instance;  // GWLP_HINSTANCE
    case -8: return &win->parent;    // GWLP_HWNDPARENT
    case -12: return &win->id;       // GWLP_ID
    case -21: return &win->userdata; // GWLP_USERDATA
    default: return NULL;
    }
}
static u64 get_long(Window *win, i32 index, int size) {
    if (index == -16) return win->style;
    if (index == -20) return win->exstyle;
    u64 *slot = long_slot(win, index);
    if (slot) return *slot;
    if (index >= 0 && (u32)index + (u32)size <= win->extra_size) {
        u64 v = 0;
        memcpy(&v, win->extra + index, (size_t)size);
        return v;
    }
    return 0;
}
static u64 set_long(Window *win, i32 index, u64 value, int size) {
    u64 old = get_long(win, index, size);
    if (index == -16) win->style = (u32)value;
    else if (index == -20) win->exstyle = (u32)value;
    else if (long_slot(win, index)) *long_slot(win, index) = value;
    else if (index >= 0 && (u32)index + (u32)size <= win->extra_size) memcpy(win->extra + index, &value, (size_t)size);
    return old;
}
static void u_GetWindowLongPtrW(RcnCtx *ctx) {
    Window *win = window_of(A0);
    RET(win ? get_long(win, (i32)A1, 8) : 0);
}
static void u_SetWindowLongPtrW(RcnCtx *ctx) {
    Window *win = window_of(A0);
    RET(win ? set_long(win, (i32)A1, A2, 8) : 0);
}
static void u_GetWindowLongA(RcnCtx *ctx) {
    Window *win = window_of(A0);
    RET_I32(win ? (i32)get_long(win, (i32)A1, 4) : 0);
}
static void u_SetWindowLongA(RcnCtx *ctx) {
    Window *win = window_of(A0);
    RET_I32(win ? (i32)set_long(win, (i32)A1, (u32)A2, 4) : 0);
}

// --- Messages ---

static void u_PeekMessageW(RcnCtx *ctx) { RET(take(ctx, A0, A1, (u32)A2, (u32)A3, ((u32)STK(4) & PM_REMOVE) != 0)); }
static void u_GetMessageW(RcnCtx *ctx) {
    u64 out = A0, hwnd = A1;
    u32 min = (u32)A2, max = (u32)A3;
    while (!take(ctx, out, hwnd, min, max, 1)) {
        pthread_mutex_lock(&us.lock);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 10000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&us.cond, &us.lock, &ts);
        pthread_mutex_unlock(&us.lock);
    }
    RET(rd32(out + (ctx->bits32 ? 4 : 8)) != WM_QUIT);
}
static void u_PostMessageW(RcnCtx *ctx) {
    post(A0, (u32)A1, A2, A3);
    RET(1);
}
// PostThreadMessageW(thread, message, w, l) : file de messages unique du jeu.
static void u_PostThreadMessageW(RcnCtx *ctx) {
    post(0, (u32)A1, A2, A3);
    RET(1);
}
static void u_PostQuitMessage(RcnCtx *ctx) { post(0, WM_QUIT, (u32)A0, 0); }
static void u_TranslateMessage(RcnCtx *ctx) { RET(0); }
static void u_DispatchMessageW(RcnCtx *ctx) {
    u64 m = A0, hwnd, w, l;
    u32 message;
    if (ctx->bits32) hwnd = rd32(m), message = rd32(m + 4), w = rd32(m + 8), l = rd32(m + 12);
    else hwnd = rd64(m), message = rd32(m + 8), w = rd64(m + 16), l = rd64(m + 24);
    if (message == WM_TIMER && l) {
        RET(rcn_call_guest(ctx, l, hwnd, WM_TIMER, w, (u32)(rcn_now_ns() / 1000000)));
        return;
    }
    // RCN_MSG_LOG=1 : messages du clavier et de la souris remis au jeu, avec la réponse de sa procédure.
    static int msg_log = -1;
    if (msg_log < 0) msg_log = getenv("RCN_MSG_LOG") != NULL;
    // WM_CHAR : le moteur range le caractère dans son objet fenêtre (+0x28). Le journal dit si l'objet est
    // là (données de la fenêtre) et si le caractère y est arrivé — et si l'interface avait consommé le
    // précédent (valeur lue avant l'envoi) : « message perdu » ou « champ qui n'en veut pas ».
    u32 char_before = 0xFFFFu;
    u64 object = 0;
    if (msg_log && message == 0x0102) {
        Window *win = window_of(hwnd);
        object = win ? win->userdata : 0;
        char_before = object ? rd32(object + 0x28) : 0xFFFFu;
    }
    u64 r = call_proc(ctx, hwnd, message, w, l);
    if (msg_log && ((message >= 0x100 && message <= 0x109) || (message >= 0x200 && message <= 0x20E) || message == 0xFF))
        rcn_log("message %04x à %llx (w %llx, l %llx) -> %llx", message, (unsigned long long)hwnd, (unsigned long long)w,
                (unsigned long long)l, (unsigned long long)r);
    if (msg_log && message == 0x0102)
        rcn_log("WM_CHAR %04llx : objet %llx, table %llx, avant %04x, apres %04x", (unsigned long long)w, (unsigned long long)object,
                (unsigned long long)(object ? rd64(object) : 0), char_before, object ? rd32(object + 0x28) : 0xFFFFu);
    if (msg_log && (message == 0x0102 || message == 0x0201)) {
        char line[160];
        int k = 0;
        for (u32 i = 0x18; i < 0x48 && k < (int)sizeof line - 12; i += 4)
            k += snprintf(line + k, sizeof line - (size_t)k, " %08x", object ? rd32(object + i) : 0);
        line[k] = 0;
        rcn_log("objet fenetre +0x18.. :%s", line);
    }
    if (msg_log && message == 0x0102 && object) rcn_char_watch(ctx->proc, object + 0x28, (u32)w);
    RET(r);
}
static void u_SendMessageW(RcnCtx *ctx) { RET(call_proc(ctx, A0, (u32)A1, A2, A3)); }
// SendMessageTimeoutA(hwnd, message, w, l, flags, timeout, &result)
static void u_SendMessageTimeoutA(RcnCtx *ctx) {
    u64 r = window_of(A0) ? call_proc(ctx, A0, (u32)A1, A2, A3) : 0;
    if (STK(6)) wrp(ctx, STK(6), r);
    RET(1);
}
// MsgWaitForMultipleObjects(count, handles, all, milliseconds, mask)
static void u_MsgWaitForMultipleObjects(RcnCtx *ctx) {
    u32 count = (u32)A0, ms = (u32)A3;
    u64 handles[64];
    for (u32 i = 0; i < count && i < 64; i++) handles[i] = rdp(ctx, A1 + PTR_SIZE(ctx) * (u64)i);
    u64 start = rcn_now_ns();
    for (;;) {
        if (take(ctx, 0, 0, 0, 0, 0)) {
            RET(count); // WAIT_OBJECT_0 + count : un message attend
            return;
        }
        u64 waited = (rcn_now_ns() - start) / 1000000;
        u32 slice = ms == 0xFFFFFFFFu ? 5 : (waited >= ms ? 0 : (ms - waited < 5 ? (u32)(ms - waited) : 5));
        u32 r = count ? rcn_wait_handles(ctx, count, handles, (u32)A2 != 0, slice) : 0x102;
        if (r != 0x102) {
            RET(r);
            return;
        }
        if (!count && slice) rcn_sleep_ms(slice);
        if (ms != 0xFFFFFFFFu && (rcn_now_ns() - start) / 1000000 >= ms) {
            RET(0x102); // WAIT_TIMEOUT
            return;
        }
    }
}
static void u_GetMessageExtraInfo(RcnCtx *ctx) { RET(0); }
static void u_RegisterWindowMessageA(RcnCtx *ctx) {
    u32 h = 5381;
    for (const char *s = STR(A0); s && *s; s++) h = h * 33 + (u8)*s;
    RET(0xC000 + h % 0x3FFF);
}
// SetTimer(hwnd, id, milliseconds, proc)
static void u_SetTimer(RcnCtx *ctx) {
    u64 id = A1 ? A1 : 0x7000;
    pthread_mutex_lock(&us.lock);
    Timer *free_slot = NULL;
    for (int i = 0; i < MAX_TIMERS; i++) {
        Timer *t = &us.timers[i];
        if (t->period && t->hwnd == A0 && t->id == id) free_slot = t;
        if (!free_slot && !t->period) free_slot = t;
    }
    if (free_slot) {
        free_slot->hwnd = A0;
        free_slot->id = id;
        free_slot->proc = A3;
        free_slot->period = (u32)A2 ? (u32)A2 : 1;
        free_slot->due_ns = rcn_now_ns() + (u64)free_slot->period * 1000000ull;
    }
    pthread_mutex_unlock(&us.lock);
    RET(free_slot ? id : 0);
}
static void u_KillTimer(RcnCtx *ctx) {
    pthread_mutex_lock(&us.lock);
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (us.timers[i].period && us.timers[i].hwnd == A0 && us.timers[i].id == A1) us.timers[i].period = 0;
    }
    pthread_mutex_unlock(&us.lock);
    RET(1);
}

// --- Fenêtres : état ---

static void u_Main(RcnCtx *ctx) { RET(us.focus ? us.focus : us.main); }
static void u_SetFocus(RcnCtx *ctx) {
    u64 previous = us.focus;
    if (window_of(A0)) us.focus = A0;
    RET(previous);
}
static void u_True(RcnCtx *ctx) { RET(1); }
static void u_Zero(RcnCtx *ctx) { RET(0); }
static void u_IsWindowVisible(RcnCtx *ctx) {
    Window *win = window_of(A0);
    RET(win && win->visible);
}
static void u_GetParent(RcnCtx *ctx) {
    Window *win = window_of(A0);
    RET(win && win->parent != (u64)-3 ? win->parent : 0);
}
static void u_GetWindowThreadProcessId(RcnCtx *ctx) {
    if (A1) wr32(A1, 1000);
    RET(1001);
}
// GetWindowPlacement(hwnd, &placement) : normale, à la taille de la fenêtre.
static void u_GetWindowPlacement(RcnCtx *ctx) {
    Window *win = window_of(A0);
    u64 p = A1;
    memset(PTR(p + 4), 0, 40);
    wr32(p + 8, 1); // SW_SHOWNORMAL
    if (win) write_rect(p + 28, win->x, win->y, win->x + win->w, win->y + win->h);
    RET(1);
}
static void u_EnumWindows(RcnCtx *ctx) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!us.windows[i].used || us.windows[i].parent) continue;
        if (!rcn_call_guest(ctx, A0, us.windows[i].hwnd, A1, 0, 0)) break;
    }
    RET(1);
}
static void u_GetDesktopWindow(RcnCtx *ctx) { RET(DESKTOP); }
static void u_FakeDc(RcnCtx *ctx) { RET(FAKE_DC); }
static void u_FakeIcon(RcnCtx *ctx) { RET(FAKE_ICON); }

// --- Écran, moniteur ---

static void u_MonitorFrom(RcnCtx *ctx) { RET(MONITOR); }
// GetMonitorInfoW / A(monitor, &info) : MONITORINFO(EX).
static void monitor_info(RcnCtx *ctx, int wide) {
    u64 m = A1;
    u32 size = rd32(m);
    write_rect(m + 4, 0, 0, SCREEN_W, SCREEN_H);
    write_rect(m + 20, 0, 0, SCREEN_W, SCREEN_H);
    wr32(m + 36, 1); // MONITORINFOF_PRIMARY
    static const char device[] = "\\\\.\\DISPLAY1";
    if (size >= (wide ? 104u : 72u)) {
        for (int i = 0; i < 32; i++) {
            u8 c = i < (int)sizeof device ? (u8)device[i] : 0;
            if (wide) wr16(m + 40 + 2 * (u64)i, c);
            else wr8(m + 40 + (u64)i, c);
        }
    }
    RET(1);
}
static void u_GetMonitorInfoW(RcnCtx *ctx) { monitor_info(ctx, 1); }
static void u_GetMonitorInfoA(RcnCtx *ctx) { monitor_info(ctx, 0); }
// EnumDisplayMonitors(dc, &clip, proc, data) : un moniteur.
static void u_EnumDisplayMonitors(RcnCtx *ctx) {
    u8 *rect = calloc(1, 16);
    if (rect) write_rect((u64)(uintptr_t)rect, 0, 0, SCREEN_W, SCREEN_H);
    u64 args[4] = {MONITOR, A0, (u64)(uintptr_t)rect, A3};
    rcn_call_guest_n(ctx, A2, 4, args);
    free(rect);
    RET(1);
}
// EnumDisplayDevicesW / A(device, index, &DISPLAY_DEVICE, flags) : sans `device`, l'unique carte
// (\\.\DISPLAY1, de bureau et principale) ; avec son nom, son unique écran (actif, attaché). Unreal Engine
// bâtit sa liste d'écrans ainsi (FDisplayMetrics) : vide, ses tailles et échelles d'écran valaient 0, d'où
// des divisions par zéro (interface de taille nulle, caméra NaN dans Clair Obscur).
// DISPLAY_DEVICE : cb, DeviceName[32], DeviceString[128], StateFlags, DeviceID[128], DeviceKey[128].
static void display_device(RcnCtx *ctx, int wide) {
    u64 device = A0, d = A2;
    if ((u32)A1 != 0 || !d) {
        RET(0);
        return;
    }
    const char *name, *text, *id;
    u32 flags;
    if (!device || (wide ? rd16(device) : rd8(device)) == 0) {
        name = "\\\\.\\DISPLAY1", text = "RomCloud Display Adapter", id = "PCI\\VEN_0000&DEV_0000";
        flags = 0x1 | 0x4; // ATTACHED_TO_DESKTOP | PRIMARY_DEVICE
    } else {
        name = "\\\\.\\DISPLAY1\\Monitor0", text = "Generic PnP Monitor";
        id = "MONITOR\\RCN0001\\{4d36e96e-e325-11ce-bfc1-08002be10318}\\0000";
        flags = 0x1 | 0x2; // DISPLAY_DEVICE_ACTIVE | DISPLAY_DEVICE_ATTACHED
    }
    u32 c = wide ? 2 : 1;
    u32 o_name = 4, o_text = o_name + 32 * c, o_flags = o_text + 128 * c, o_id = o_flags + 4, o_key = o_id + 128 * c;
    memset(PTR(d + 4), 0, o_key + 128 * c - 4);
    const char *fields[3] = {name, text, id};
    u32 offsets[3] = {o_name, o_text, o_id}, sizes[3] = {32, 128, 128};
    for (int f = 0; f < 3; f++)
        for (u32 i = 0; fields[f][i] && i + 1 < sizes[f]; i++) {
            if (wide) wr16(d + offsets[f] + 2ull * i, (u8)fields[f][i]);
            else wr8(d + offsets[f] + i, (u8)fields[f][i]);
        }
    wr32(d + o_flags, flags);
    RET(1);
}
static void u_EnumDisplayDevicesA(RcnCtx *ctx) { display_device(ctx, 0); }
static void u_EnumDisplayDevicesW(RcnCtx *ctx) { display_device(ctx, 1); }
// EnumDisplaySettingsW / A(device, mode, &DEVMODE) : le seul mode, 1920 × 1080 à 60 Hz.
static void display_settings(RcnCtx *ctx, int wide) {
    i32 mode = (i32)A1;
    if (mode != -1 && mode != -2 && mode != 0) {
        RET(0);
        return;
    }
    u64 d = A2;
    u32 base = wide ? 64 : 32;          // après dmDeviceName
    u32 tail = wide ? 168 : 104;        // dmBitsPerPel
    wr16(d + base + 4, (u16)(wide ? 220 : 156)); // dmSize
    wr32(d + base + 8, 0x5C0000);       // dmFields : bits, largeur, hauteur, fréquence
    wr32(d + tail, 32);
    wr32(d + tail + 4, SCREEN_W);
    wr32(d + tail + 8, SCREEN_H);
    wr32(d + tail + 12, 0);
    wr32(d + tail + 16, 60);
    RET(1);
}
static void u_EnumDisplaySettingsW(RcnCtx *ctx) { display_settings(ctx, 1); }
static void u_EnumDisplaySettingsA(RcnCtx *ctx) { display_settings(ctx, 0); }
static void u_NotSupported(RcnCtx *ctx) { RET(50); } // ERROR_NOT_SUPPORTED
// GetDisplayConfigBufferSizes(drapeaux, &chemins, &modes) : aucune configuration d'affichage à énumérer sur
// Android, mais les deux compteurs doivent être écrits — le jeu les lit sans vérifier le résultat, et des
// valeurs non initialisées lui font réserver un vecteur de plusieurs giga-octets (NieR: Automata : 5 Go).
static void u_GetDisplayConfigBufferSizes(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    if (A2) wr32(A2, 0);
    RET(0); // ERROR_SUCCESS
}
// QueryDisplayConfig(drapeaux, &chemins, chemins, &modes, modes, &topologie) : rien à rendre, compteurs à zéro.
static void u_QueryDisplayConfig(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    if (A3) wr32(A3, 0);
    if (STK(5)) wr32(STK(5), 0);
    RET(0);
}
static void u_GetSystemMetrics(RcnCtx *ctx) {
    switch ((u32)A0) {
    case 0: case 16: case 78: RET(SCREEN_W); return; // SM_CXSCREEN, SM_CXFULLSCREEN, SM_CXVIRTUALSCREEN
    case 1: case 17: case 79: RET(SCREEN_H); return;
    case 80: RET(1); return;                          // SM_CMONITORS
    case 13: case 14: RET(32); return;                // curseur
    case 43: RET(3); return;                          // SM_CMOUSEBUTTONS
    default: RET(0); return;
    }
}
// SystemParametersInfoW(action, value, &data, flags)
static void u_SystemParametersInfoW(RcnCtx *ctx) {
    if ((u32)A0 == 0x30 && A2) write_rect(A2, 0, 0, SCREEN_W, SCREEN_H); // SPI_GETWORKAREA
    RET(1);
}

// --- Entrées (absentes : la manette passe par XInput) ---

static void u_GetKeyboardLayout(RcnCtx *ctx) { RET(0x04090409); }
static void u_GetKeyboardLayoutNameW(RcnCtx *ctx) {
    static const char name[] = "00000409";
    for (int i = 0; i < 9; i++) wr16(A0 + 2 * (u64)i, (u8)name[i]);
    RET(1);
}
// Pointeur : là où le jeu l'a placé (SetCursorPos), au centre de l'écran au départ. Les interfaces
// d'Unreal Engine (CommonUI) changent la validation à la manette en clic simulé à l'endroit du bouton
// choisi, après y avoir mis le pointeur ; un pointeur figé ailleurs, ou hors de la fenêtre du jeu selon
// WindowFromPoint, et aucun bouton ne s'active (Clair Obscur : « Nouvelle partie » sans effet).
static volatile i32 cursor_x = 960, cursor_y = 540;
// Boutons enfoncés (MK_LBUTTON 1, MK_RBUTTON 2, MK_MBUTTON 0x10) et position à la dernière lecture
// DirectInput (déplacements relatifs).
static volatile u32 mouse_down;
static volatile i32 mouse_read_x = 960, mouse_read_y = 540;
static void u_GetCursorPos(RcnCtx *ctx) {
    if (!A0) {
        RET(0);
        return;
    }
    wr32(A0, (u32)cursor_x);
    wr32(A0 + 4, (u32)cursor_y);
    RET(1);
}
static void u_SetCursorPos(RcnCtx *ctx) {
    i32 x = (i32)A0, y = (i32)A1;
    cursor_x = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
    cursor_y = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
    RET(1);
}
// Capture de la souris (SetCapture / GetCapture / ReleaseCapture) : la fenêtre qui la tient.
static volatile u64 capture_hwnd;
static void u_SetCapture(RcnCtx *ctx) { RET(__atomic_exchange_n(&capture_hwnd, A0, __ATOMIC_ACQ_REL)); }
static void u_GetCapture(RcnCtx *ctx) { RET(capture_hwnd); }
static void u_ReleaseCapture(RcnCtx *ctx) {
    capture_hwnd = 0;
    RET(1);
}
// SendInput(n, INPUT[], taille) : entrées injectées, rendues en messages de fenêtre comme sous Windows.
// Souris (type 0) : déplacement (relatif, ou absolu sur 0-65535), boutons, molette → WM_MOUSEMOVE,
// WM_[LRM]BUTTONDOWN/UP, WM_MOUSEWHEEL, à la fenêtre qui tient la capture, sinon à celle sous le pointeur.
// Clavier (type 1) → WM_KEYDOWN / WM_KEYUP à la fenêtre active. CommonUI (Unreal Engine) valide ainsi un
// bouton à la manette : clic gauche simulé à l'endroit du bouton.
// INPUT (40 octets en 64 bits) : type, puis MOUSEINPUT { dx, dy, mouseData, dwFlags, time, extra } ou
// KEYBDINPUT { wVk, wScan, dwFlags, time, extra }.
static void u_SendInput(RcnCtx *ctx) {
    u32 n = (u32)A0, size = (u32)A2;
    u64 list = A1;
    // INPUT : type, puis l'union (mouse / keyboard) à +4 en 32 bits (28 octets), +8 en 64 bits (40 octets).
    u32 u = ctx->bits32 ? 4 : 8;
    if (!list || size < (ctx->bits32 ? 28u : 40u)) {
        RET(0);
        return;
    }
    static u32 buttons; // MK_LBUTTON 1, MK_RBUTTON 2, MK_MBUTTON 0x10
    for (u32 i = 0; i < n; i++) {
        u64 in = list + (u64)i * size;
        u32 type = rd32(in);
        static int told;
        if (told++ < 40)
            rcn_log("SendInput : type %u, %08x %08x %08x %08x (pointeur %d,%d)", type, rd32(in + u), rd32(in + u + 4), rd32(in + u + 8),
                    rd32(in + u + 12), cursor_x, cursor_y);
        if (type == 0) {
            i32 dx = (i32)rd32(in + u), dy = (i32)rd32(in + u + 4);
            u32 data = rd32(in + u + 8), flags = rd32(in + u + 12);
            if (flags & 0x1) { // MOUSEEVENTF_MOVE
                i32 x = flags & 0x8000 ? (i32)((i64)dx * SCREEN_W / 65536) : cursor_x + dx;
                i32 y = flags & 0x8000 ? (i32)((i64)dy * SCREEN_H / 65536) : cursor_y + dy;
                cursor_x = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
                cursor_y = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
            }
            u64 target = capture_hwnd;
            if (!target) {
                i32 px = cursor_x, py = cursor_y;
                pthread_mutex_lock(&us.lock);
                for (int k = MAX_WINDOWS - 1; k >= 0 && !target; k--) {
                    Window *w = &us.windows[k];
                    if (w->used && w->visible && (!w->parent || w->parent == DESKTOP) && px >= w->x && px < w->x + w->w &&
                        py >= w->y && py < w->y + w->h)
                        target = w->hwnd;
                }
                if (!target) target = us.main;
                pthread_mutex_unlock(&us.lock);
            }
            Window *win = window_of(target);
            i32 cx = cursor_x - (win ? win->x : 0), cy = cursor_y - (win ? win->y : 0);
            u64 l = (u64)(u16)cx | (u64)(u16)cy << 16;
            static const struct {
                u32 flag, message, mk;
                int down;
            } map[] = {{0x2, 0x201, 1, 1}, {0x4, 0x202, 1, 0}, {0x8, 0x204, 2, 1}, {0x10, 0x205, 2, 0}, {0x20, 0x207, 0x10, 1}, {0x40, 0x208, 0x10, 0}};
            if (flags & 0x1) post(target, 0x200, buttons, l); // WM_MOUSEMOVE
            for (size_t k = 0; k < sizeof map / sizeof map[0]; k++) {
                if (!(flags & map[k].flag)) continue;
                if (map[k].down) buttons |= map[k].mk;
                else buttons &= ~map[k].mk;
                post(target, map[k].message, buttons, l);
            }
            if (flags & 0x800) { // MOUSEEVENTF_WHEEL : coordonnées d'écran dans lParam
                u64 screen = (u64)(u16)cursor_x | (u64)(u16)cursor_y << 16;
                post(target, 0x20A, (u64)(u16)data << 16 | buttons, screen);
            }
        } else if (type == 1) {
            u16 vk = rd16(in + u), scan = rd16(in + u + 2);
            u32 flags = rd32(in + u + 4);
            u64 target = us.focus ? us.focus : us.main;
            u64 l = 1 | (u64)(scan & 0xFF) << 16 | (flags & 0x1 ? 1ull << 24 : 0); // KEYEVENTF_EXTENDEDKEY
            if (flags & 0x2) post(target, 0x0101, vk, l | 0xC0000000ull); // KEYEVENTF_KEYUP
            else post(target, 0x0100, vk, l);
        }
    }
    RET(n);
}
// RegisterRawInputDevices(RAWINPUTDEVICE[], n, taille) : { usUsagePage, usUsage, dwFlags, hwndTarget }.
// Noté (souris 1/2, clavier 1/6, RIDEV_NOLEGACY 0x30, RIDEV_INPUTSINK 0x100).
static struct {
    u16 page, usage;
    u32 flags;
    u64 hwnd;
} raw_devices[8];
static int raw_device_count;
static void u_RegisterRawInputDevices(RcnCtx *ctx) {
    u32 n = (u32)A1, size = (u32)A2;
    for (u32 i = 0; i < n && size >= (ctx->bits32 ? 12u : 16u); i++) {
        u64 d = A0 + (u64)i * size;
        u16 page = rd16(d), usage = rd16(d + 2);
        u32 flags = rd32(d + 4);
        u64 hwnd = rdp(ctx, d + 8);
        rcn_log("entrée brute : page %u, usage %u, drapeaux %x, fenêtre %llx", page, usage, flags, (unsigned long long)hwnd);
        int k = 0;
        while (k < raw_device_count && !(raw_devices[k].page == page && raw_devices[k].usage == usage)) k++;
        if (k == raw_device_count && raw_device_count < 8) raw_device_count++;
        if (k < 8) raw_devices[k].page = page, raw_devices[k].usage = usage, raw_devices[k].flags = flags, raw_devices[k].hwnd = hwnd;
    }
    RET(1);
}
// Fenêtre qui reçoit la souris : celle qui tient la capture, sinon celle sous le pointeur, sinon la
// principale. Coordonnées client écrites dans *cx, *cy.
static u64 mouse_target_at(i32 *cx, i32 *cy) {
    u64 target = capture_hwnd;
    if (!target) {
        pthread_mutex_lock(&us.lock);
        for (int k = MAX_WINDOWS - 1; k >= 0 && !target; k--) {
            Window *w = &us.windows[k];
            if (w->used && w->visible && (!w->parent || w->parent == DESKTOP) && cursor_x >= w->x &&
                cursor_x < w->x + w->w && cursor_y >= w->y && cursor_y < w->y + w->h)
                target = w->hwnd;
        }
        if (!target) target = us.main;
        pthread_mutex_unlock(&us.lock);
    }
    Window *win = window_of(target);
    *cx = cursor_x - (win ? win->x : 0);
    *cy = cursor_y - (win ? win->y : 0);
    return target;
}
// Place le pointeur, ramené dans l'écran imité ; vrai s'il a bougé.
static int place_cursor(i32 x, i32 y) {
    int moved = x != cursor_x || y != cursor_y;
    cursor_x = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
    cursor_y = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
    return moved;
}
// Clic de l'hôte (pointeur de mise au point, toucher de l'écran) : pointeur placé en (x, y) de l'écran
// imité, puis WM_MOUSEMOVE et bouton gauche enfoncé ou relâché à la fenêtre sous le pointeur.
void rcn_user32_mouse(int x, int y, int down) {
    // Position de l'hôte sur un écran de 1920 × 1080 : ramenée à l'écran imité.
    place_cursor((i32)((i64)x * SCREEN_W / 1920), (i32)((i64)y * SCREEN_H / 1080));
    if (down) mouse_down |= 1;
    else mouse_down &= ~1u;
    i32 cx, cy;
    u64 target = mouse_target_at(&cx, &cy);
    u64 l = (u64)(u16)cx | (u64)(u16)cy << 16;
    post(target, 0x200, mouse_down, l);
    post(target, down ? 0x201 : 0x202, mouse_down, l);
}
// Souris de l'appareil (toucher de l'image du jeu) : position en 1/65535 de l'écran imité.
void rcn_host_mouse_pos(int x, int y) {
    if (!place_cursor((i32)((i64)x * SCREEN_W / 65536), (i32)((i64)y * SCREEN_H / 65536))) return;
    i32 cx, cy;
    u64 target = mouse_target_at(&cx, &cy);
    if (!target) return;
    u64 l = (u64)(u16)cx | (u64)(u16)cy << 16;
    post(target, 0x200, mouse_down, l); // WM_MOUSEMOVE
}
// Bouton de la souris de l'appareil (0 gauche, 1 droit, 2 milieu), enfoncé ou relâché.
void rcn_host_mouse_button(int button, int down) {
    static const struct {
        u32 mask, up, press;
    } map[] = {{1, 0x202, 0x201}, {2, 0x205, 0x204}, {0x10, 0x208, 0x207}};
    if (button < 0 || button > 2) return;
    if (down) mouse_down |= map[button].mask;
    else mouse_down &= ~map[button].mask;
    i32 cx, cy;
    u64 target = mouse_target_at(&cx, &cy);
    u64 l = (u64)(u16)cx | (u64)(u16)cy << 16;
    post(target, down ? map[button].press : map[button].up, mouse_down, l);
}
// DirectInput : déplacement du pointeur depuis la lecture précédente et boutons enfoncés (MK_*).
void rcn_mouse_state(int *dx, int *dy, u32 *buttons) {
    *dx = cursor_x - mouse_read_x;
    *dy = cursor_y - mouse_read_y;
    mouse_read_x = cursor_x;
    mouse_read_y = cursor_y;
    *buttons = mouse_down;
}
// WindowFromPoint(POINT par valeur, x en bas, y en haut) : la fenêtre visible de premier niveau qui
// contient le point (la plus récente), sinon la fenêtre principale du jeu.
static void u_WindowFromPoint(RcnCtx *ctx) {
    // POINT par valeur : un registre en 64 bits, deux cases de pile en 32 bits.
    i32 x = (i32)(u32)A0, y = ctx->bits32 ? (i32)(u32)A1 : (i32)(u32)(A0 >> 32);
    u64 found = 0;
    pthread_mutex_lock(&us.lock);
    for (int i = MAX_WINDOWS - 1; i >= 0 && !found; i--) {
        Window *w = &us.windows[i];
        if (w->used && w->visible && (!w->parent || w->parent == DESKTOP) && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) found = w->hwnd;
    }
    if (!found) found = us.main;
    pthread_mutex_unlock(&us.lock);
    RET(found);
}
static void u_ShowCursor(RcnCtx *ctx) { RET_I32((u32)A0 ? ++us.cursor_count : --us.cursor_count); }
// SwapMouseButton(échanger) : les boutons restent dans l'ordre habituel ; rend l'état précédent (faux).
static void u_SwapMouseButton(RcnCtx *ctx) { (void)ctx; RET(0); }
// Entrée brute : un clavier et une souris (tout PC en a ; Unity les attend).
enum { RIM_TYPEMOUSE = 0, RIM_TYPEKEYBOARD = 1, RAW_MOUSE = 0x52494d30, RAW_KEYBOARD = 0x52494b30 };
// GetRawInputDeviceList(RAWINPUTDEVICELIST *list, &count, size) : { handle, type } de 16 octets.
static void u_GetRawInputDeviceList(RcnCtx *ctx) {
    u64 list = A0, count = A1;
    if (!count) {
        RET_I32(-1);
        return;
    }
    if (!list) {
        wr32(count, 2);
        RET(0);
        return;
    }
    if (rd32(count) < 2) {
        wr32(count, 2);
        LAST_ERROR(ctx) = 122; // ERROR_INSUFFICIENT_BUFFER
        RET_I32(-1);
        return;
    }
    // RAWINPUTDEVICELIST : { handle, type } de 8 octets en 32 bits, 16 en 64 bits.
    u32 step = ctx->bits32 ? 8 : 16, psz = PTR_SIZE(ctx);
    wrp(ctx, list, RAW_KEYBOARD);
    wr32(list + psz, RIM_TYPEKEYBOARD);
    wrp(ctx, list + step, RAW_MOUSE);
    wr32(list + step + psz, RIM_TYPEMOUSE);
    RET(2);
}
// GetRawInputDeviceInfoA / W(handle, command, data, &size) : nom ou RID_DEVICE_INFO.
static void raw_device_info(RcnCtx *ctx, int wide) {
    u64 h = A0, data = A2, size_at = A3;
    u32 command = (u32)A1;
    int keyboard = h == RAW_KEYBOARD;
    if ((h != RAW_KEYBOARD && h != RAW_MOUSE) || !size_at) {
        RET_I32(-1);
        return;
    }
    if (command == 0x20000007) { // RIDI_DEVICENAME (en caractères)
        const char *name = keyboard ? "\\\\?\\ACPI#PNP0303#0#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}"
                                    : "\\\\?\\ACPI#PNP0F13#0#{378de44c-56ef-11d1-bc8c-00a0c91405dd}";
        u32 n = (u32)strlen(name) + 1;
        if (!data || rd32(size_at) < n) {
            wr32(size_at, n);
            RET_I32(data ? -1 : 0);
            return;
        }
        for (u32 i = 0; i < n; i++) {
            if (wide) wr16(data + 2 * i, (u8)name[i]);
            else wr8(data + i, (u8)name[i]);
        }
        RET(n);
        return;
    }
    if (command == 0x2000000b) { // RIDI_DEVICEINFO : 32 octets
        if (!data || rd32(size_at) < 32) {
            wr32(size_at, 32);
            RET_I32(data ? -1 : 0);
            return;
        }
        memset(PTR(data), 0, 32);
        wr32(data, 32);
        wr32(data + 4, keyboard ? RIM_TYPEKEYBOARD : RIM_TYPEMOUSE);
        if (keyboard) {
            u32 kb[6] = {4, 0, 1, 12, 3, 101}; // type, sous-type, mode, touches de fonction, voyants, touches
            memcpy(PTR(data + 8), kb, sizeof kb);
        } else {
            u32 ms[4] = {1, 5, 0, 1}; // identifiant, boutons, fréquence, molette horizontale
            memcpy(PTR(data + 8), ms, sizeof ms);
        }
        RET(32);
        return;
    }
    wr32(size_at, 0);
    RET_I32(-1);
}
static void u_GetRawInputDeviceInfoW(RcnCtx *ctx) { raw_device_info(ctx, 1); }
static void u_GetRawInputDeviceInfoA(RcnCtx *ctx) { raw_device_info(ctx, 0); }
static void u_MinusOne(RcnCtx *ctx) { RET_I32(-1); }
static void u_GetCaretBlinkTime(RcnCtx *ctx) { RET(530); }
static void u_GetDoubleClickTime(RcnCtx *ctx) { RET(500); }
// MessageBoxA(hwnd, text, caption, type) : vers la sortie d'erreur, réponse « OK ».
static void u_MessageBoxA(RcnCtx *ctx) {
    rcn_log("rcn : boite de message « %s » : %s", A2 ? STR(A2) : "", A1 ? STR(A1) : "");
    RET(1);
}

// --- Rectangles ---

static void u_CopyRect(RcnCtx *ctx) {
    memcpy(PTR(A0), PTR(A1), 16);
    RET(1);
}
static void u_OffsetRect(RcnCtx *ctx) {
    i32 dx = (i32)A1, dy = (i32)A2;
    write_rect(A0, (i32)rd32(A0) + dx, (i32)rd32(A0 + 4) + dy, (i32)rd32(A0 + 8) + dx, (i32)rd32(A0 + 12) + dy);
    RET(1);
}
// PtInRect(&rect, point) : le point (x, y) est passé par valeur dans un registre de 64 bits.
static void u_PtInRect(RcnCtx *ctx) {
    i32 x = (i32)(u32)A1, y = (i32)(u32)(A1 >> 32);
    RET(x >= (i32)rd32(A0) && x < (i32)rd32(A0 + 8) && y >= (i32)rd32(A0 + 4) && y < (i32)rd32(A0 + 12));
}

// Station de fenêtres du processus : la session interactive « WinSta0 ».
static void u_GetProcessWindowStation(RcnCtx *ctx) { RET(0x5A0); }
// GetUserObjectInformationW(objet, index, données, taille, &nécessaire) : UOI_NAME (2) « WinSta0 »,
// UOI_FLAGS (1) visible (WSF_VISIBLE).
static void u_GetUserObjectInformationW(RcnCtx *ctx) {
    u32 index = (u32)A1, size = (u32)A3;
    u64 data = A2, needed = STK(4);
    if (index == 1) {
        if (needed) wr32(needed, 12);
        if (!data || size < 12) {
            LAST_ERROR(ctx) = 122;
            RET(0);
            return;
        }
        wr32(data, 0), wr32(data + 4, 0), wr32(data + 8, 1);
        RET(1);
        return;
    }
    static const char name[] = "WinSta0";
    u32 bytes = 2 * (u32)sizeof name;
    if (needed) wr32(needed, bytes);
    if (index != 2 || !data || size < bytes) {
        LAST_ERROR(ctx) = index == 2 ? 122 : 87;
        RET(0);
        return;
    }
    for (u32 i = 0; i < sizeof name; i++) wr16(data + 2 * i, (u8)name[i]);
    RET(1);
}

// LoadStringA / W(instance, identifiant, tampon, taille) : pas de ressources de texte.
static void u_LoadString(RcnCtx *ctx) {
    if (A2 && (u32)A3) wr8(A2, 0), wr16(A2, 0);
    RET(0);
}
// MessageBoxW(fenêtre, texte, titre, type) : vers le journal, réponse « OK ».
static void u_MessageBoxW(RcnCtx *ctx) {
    char *text = A1 ? rcn_utf16_to_utf8(PTR(A1), -1) : NULL, *title = A2 ? rcn_utf16_to_utf8(PTR(A2), -1) : NULL;
    rcn_log("rcn : boite de message « %s » : %s", title ? title : "", text ? text : "");
    free(text);
    free(title);
    RET(1);
}
// CallWindowProcW(procédure, fenêtre, message, wParam, lParam)
static void u_CallWindowProcW(RcnCtx *ctx) {
    u64 args[4] = {A1, A2, A3, STK(4)};
    RET(A0 ? rcn_call_guest_n(ctx, A0, 4, args) : 0);
}

// --- Variantes ANSI (jeux qui ne passent pas par les « W ») ---

// Texte ANSI (ou numéro de classe sous 0x10000) → copie UTF-16 dans la mémoire du jeu, à libérer.
static u64 wide_of(RcnCtx *ctx, u64 text) {
    if (text < 0x10000) return text;
    size_t n = strlen(STR(text));
    u16 *w = rcn_galloc(ctx, 2 * (n + 1), 0);
    if (!w) return 0;
    for (size_t i = 0; i <= n; i++) w[i] = (u8)STR(text)[i];
    return (u64)(uintptr_t)w;
}
// RegisterClassA(&wndclass) : nom de classe en ANSI.
static void register_class_a(RcnCtx *ctx, int ex) {
    u64 c = A0, name, proc;
    u32 extra;
    if (ctx->bits32) name = rd32(c + (ex ? 40 : 36)), proc = rd32(c + (ex ? 8 : 4)), extra = rd32(c + (ex ? 16 : 12));
    else name = rd64(c + 64), proc = rd64(c + 8), extra = rd32(c + 20);
    name = name >= 0x10000 ? wide_of(ctx, name) : name;
    RET(add_class(ctx, name, proc, extra));
    if (name >= 0x10000) rcn_gfree(ctx, PTR(name));
}
static void u_RegisterClassA(RcnCtx *ctx) { register_class_a(ctx, 0); }
static void u_RegisterClassExA(RcnCtx *ctx) { register_class_a(ctx, 1); }
// CreateWindowExA : classe et titre recopiés en UTF-16, puis CreateWindowExW.
static void u_CreateWindowExA(RcnCtx *ctx) {
    u64 cls = wide_of(ctx, A1), title = A2 ? wide_of(ctx, A2) : 0;
    ctx->r[2] = cls;
    ctx->r[8] = title;
    u_CreateWindowExW(ctx);
    if (cls >= 0x10000) rcn_gfree(ctx, PTR(cls));
    if (title) rcn_gfree(ctx, PTR(title));
}
// SetRect(&rect, gauche, haut, droite, bas)
static void u_SetRect(RcnCtx *ctx) {
    if (!A0) {
        RET(0);
        return;
    }
    wr32(A0, (u32)A1), wr32(A0 + 4, (u32)A2), wr32(A0 + 8, (u32)A3), wr32(A0 + 12, (u32)STK(4));
    RET(1);
}
// GetWindowInfo(fenêtre, &WINDOWINFO) : cadre et zone cliente confondus (pas de bords).
static void u_GetWindowInfo(RcnCtx *ctx) {
    Window *win = window_of(A0);
    if (!win || !A1) {
        RET(0);
        return;
    }
    u64 p = A1;
    for (int r = 0; r < 2; r++) {
        u64 at = p + 4 + 16 * (u64)r;
        wr32(at, (u32)win->x), wr32(at + 4, (u32)win->y);
        wr32(at + 8, (u32)(win->x + win->w)), wr32(at + 12, (u32)(win->y + win->h));
    }
    wr32(p + 36, win->style), wr32(p + 40, win->exstyle), wr32(p + 44, 1);
    wr32(p + 48, 0), wr32(p + 52, 0), wr16(p + 56, 0), wr16(p + 58, 0x0500);
    RET(1);
}
// GetWindowTextA / GetClassNameA(fenêtre, tampon, taille) : texte vide.
static void u_EmptyText(RcnCtx *ctx) {
    if (A1 && (i32)A2 > 0) wr8(A1, 0);
    RET(0);
}
// Crochets (SetWindowsHookEx) : acceptés, jamais appelés.
static void u_FakeHook(RcnCtx *ctx) { RET(0x4800); }


// --- Unreal Engine 5 : fenêtres, menus et dessins sans effet ---

static void u_One(RcnCtx *ctx) { RET(1); }
// IntersectRect(&résultat, &a, &b) : intersection, vide (zéros, FALSE) sans recouvrement.
static void u_IntersectRect(RcnCtx *ctx) {
    if (!A0 || !A1 || !A2) {
        RET(0);
        return;
    }
    i32 l = (i32)rd32(A1) > (i32)rd32(A2) ? (i32)rd32(A1) : (i32)rd32(A2);
    i32 t = (i32)rd32(A1 + 4) > (i32)rd32(A2 + 4) ? (i32)rd32(A1 + 4) : (i32)rd32(A2 + 4);
    i32 r = (i32)rd32(A1 + 8) < (i32)rd32(A2 + 8) ? (i32)rd32(A1 + 8) : (i32)rd32(A2 + 8);
    i32 b = (i32)rd32(A1 + 12) < (i32)rd32(A2 + 12) ? (i32)rd32(A1 + 12) : (i32)rd32(A2 + 12);
    int ok = l < r && t < b;
    wr32(A0, ok ? (u32)l : 0), wr32(A0 + 4, ok ? (u32)t : 0), wr32(A0 + 8, ok ? (u32)r : 0), wr32(A0 + 12, ok ? (u32)b : 0);
    RET(ok);
}
// CreateDialogParamW : boîte de dialogue (écran de démarrage…) jamais montrée ; handle factice, que le jeu
// garde (sans handle, certains s'arrêtent).
static void u_DialogHandle(RcnCtx *ctx) {
    // Textes UTF-16 désignés par les premiers mots du paramètre (message, titre), pour la mise au point.
    if (getenv("RCN_PRINTF_LOG") && STK(4)) {
        for (int i = 0; i < 8; i++) {
            u64 p = rd64(STK(4) + 8ull * (u64)i);
            if (p < 0x10000 || p >> 48) continue;
            const u16 *w = PTR(p);
            char text[200];
            int n = 0;
            while (n < 199 && w[n] && w[n] < 127) text[n] = w[n] < 32 ? ' ' : (char)w[n], n++;
            text[n] = 0;
            if (n >= 3) fprintf(stderr, "rcn : dialogue %d (#%u) : %s\n", i, (u32)A1, text);
        }
    }
    RET(0xD1A10000u);
}
static void u_First(RcnCtx *ctx) { RET(A0); }
static void u_IsWindow(RcnCtx *ctx) { RET(A0 != 0); }
static void u_EmptyTextW(RcnCtx *ctx) {
    if (A1 && (i32)A2 > 0) wr16(A1, 0);
    RET(0);
}
// CharUpperW(texte ou caractère) : mot haut nul, un caractère ; sinon le texte, changé sur place.
static void u_CharUpperW(RcnCtx *ctx) {
    if (!(A0 >> 16)) {
        u32 c = (u32)A0;
        RET(c >= 'a' && c <= 'z' ? c - 32 : c);
        return;
    }
    for (u16 *t = PTR(A0); *t; t++)
        if (*t >= 'a' && *t <= 'z') *t -= 32;
    RET(A0);
}
// BeginPaint(fenêtre, &PAINTSTRUCT) : rien à dessiner.
static void u_BeginPaint(RcnCtx *ctx) {
    if (A1) memset(PTR(A1), 0, ctx->bits32 ? 64 : 72); // PAINTSTRUCT
    RET(1);
}
// GetCursorInfo(&CURSORINFO) : { cbSize, flags (1 : montré), hCursor, ptScreen }.
static void u_GetCursorInfo(RcnCtx *ctx) {
    u64 p = A0;
    if (!p) {
        RET(0);
        return;
    }
    u32 pt = ctx->bits32 ? 12 : 16;
    wr32(p + 4, us.cursor_count >= 0 ? 1 : 0);
    wrp(ctx, p + 8, 0x10003);
    wr32(p + pt, (u32)cursor_x), wr32(p + pt + 4, (u32)cursor_y);
    RET(1);
}
// GetClipCursor(&RECT) : curseur libre (rectangle nul).
static void u_GetClipCursor(RcnCtx *ctx) {
    if (A0) memset(PTR(A0), 0, 16);
    RET(1);
}
// Disposition du clavier : américaine.
enum { LAYOUT_US = 0x04090409 };
static void u_KeyboardLayout(RcnCtx *ctx) { RET(LAYOUT_US); }
static void u_GetKeyboardLayoutList(RcnCtx *ctx) {
    if ((i32)A0 > 0 && A1) wrp(ctx, A1, LAYOUT_US);
    RET(1);
}

const RcnShim rcn_user32_x64[] = {
    {"LoadAcceleratorsW", u_One, 0},
    {"IntersectRect", u_IntersectRect, 0},
    {"DestroyAcceleratorTable", u_True, 0},
    {"TranslateAcceleratorW", u_Zero, 0},
    {"IsDialogMessageW", u_Zero, 0},
    {"DisableProcessWindowsGhosting", u_Zero, 0},
    {"GetSysColor", u_Zero, 0},
    {"GetSysColorBrush", u_One, 0},
    {"CharUpperW", u_CharUpperW, 0},
    {"IsWindow", u_IsWindow, S32(1)},
    {"IsWindowEnabled", u_True, S32(1)},
    {"EnableWindow", u_Zero, S32(2)},
    {"IsZoomed", u_Zero, 0},
    {"IsChild", u_Zero, 0},
    {"GetCapture", u_GetCapture, S32(0)},
    {"SetActiveWindow", u_First, S32(1)},
    {"WindowFromPoint", u_WindowFromPoint, S32(2)},
    {"GetClipCursor", u_GetClipCursor, S32(1)},
    {"GetCursorInfo", u_GetCursorInfo, S32(1)},
    {"GetWindowTextLengthW", u_Zero, 0},
    {"GetWindowTextW", u_EmptyTextW, S32(3)},
    {"GetClassLongPtrW", u_Zero, 0},
    {"GetDCEx", u_One, 0},
    {"InvalidateRect", u_True, S32(3)},
    {"RedrawWindow", u_True, 0},
    {"BeginPaint", u_BeginPaint, S32(2)},
    {"EndPaint", u_True, S32(2)},
    {"FillRect", u_True, 0},
    {"DrawTextW", u_Zero, 0},
    {"DrawIconEx", u_True, 0},
    {"DrawStateW", u_True, S32(10)},
    {"MapWindowPoints", u_Zero, S32(4)},
    {"SetWindowRgn", u_True, 0},
    {"SetLayeredWindowAttributes", u_True, 0},
    {"SetWindowPlacement", u_True, S32(2)},
    {"FlashWindowEx", u_Zero, 0},
    {"GetWindowDisplayAffinity", u_True, 0},
    {"SetWindowDisplayAffinity", u_True, 0},
    {"RegisterTouchWindow", u_True, 0},
    {"GetTouchInputInfo", u_Zero, 0},
    {"CloseTouchInputHandle", u_True, 0},
    {"AddClipboardFormatListener", u_True, 0},
    {"SendInput", u_SendInput, S32(3)},
    {"GetKeyboardLayoutList", u_GetKeyboardLayoutList, 0},
    {"LoadKeyboardLayoutW", u_KeyboardLayout, S32(2)},
    {"ActivateKeyboardLayout", u_KeyboardLayout, 0},
    {"UnloadKeyboardLayout", u_True, S32(1)},
    {"EnumDisplayDevicesW", u_EnumDisplayDevicesW, S32(4)},
    {"ChangeDisplaySettingsW", u_Zero, 0},
    {"ChangeDisplaySettingsExW", u_Zero, 0},
    {"CreatePopupMenu", u_One, 0},
    {"InsertMenuItemW", u_True, 0},
    {"DeleteMenu", u_True, 0},
    {"EnableMenuItem", u_Zero, 0},
    {"GetSystemMenu", u_Zero, 0},
    {"TrackPopupMenu", u_Zero, 0},
    {"CreateDialogParamW", u_DialogHandle, 0},
    {"GetDlgItemTextW", u_Zero, 0},
    {"CreateIconIndirect", u_One, 0},
    {"LoadCursorFromFileW", u_Zero, 0},
    {"RegisterClassA", u_RegisterClassA, S32(1)},
    {"RegisterClassExA", u_RegisterClassExA, S32(1)},
    {"UnregisterClassA", u_True, S32(2)},
    {"CreateWindowExA", u_CreateWindowExA, 0},
    {"AdjustWindowRect", u_True, S32(3)},
    {"SetRect", u_SetRect, 0},
    {"GetWindowInfo", u_GetWindowInfo, 0},
    {"GetWindowTextA", u_EmptyText, 0},
    {"GetClassNameA", u_EmptyText, 0},
    {"SetWindowTextA", u_True, 0},
    {"SetWindowsHookExA", u_FakeHook, 0},
    {"SetWindowsHookExW", u_FakeHook, S32(4)},
    {"UnhookWindowsHookEx", u_True, S32(1)},
    {"CallNextHookEx", u_Zero, S32(4)},
    {"EnumChildWindows", u_True, 0},
    {"FindWindowA", u_Zero, 0},
    {"FindWindowW", u_Zero, 0},
    {"GetDlgItem", u_Zero, 0},
    {"GetTopWindow", u_Zero, S32(1)},
    {"LoadStringA", u_LoadString, 0},
    {"LoadStringW", u_LoadString, 0},
    {"MessageBoxW", u_MessageBoxW, S32(4)},
    {"WaitForInputIdle", u_Zero, 0},
    {"CallWindowProcW", u_CallWindowProcW, S32(5)},
    {"GetProcessWindowStation", u_GetProcessWindowStation, 0},
    {"GetUserObjectInformationW", u_GetUserObjectInformationW, 0},
    {"AdjustWindowRectEx", u_AdjustWindowRectEx, 0},
    {"AllowSetForegroundWindow", u_True, 0},
    {"ClientToScreen", u_ClientToScreen, S32(2)},
    {"ClipCursor", u_True, S32(1)},
    {"SwapMouseButton", u_SwapMouseButton, S32(1)},
    {"CloseClipboard", u_True, S32(0)},
    {"CopyRect", u_CopyRect, 0},
    {"CreateWindowExW", u_CreateWindowExW, S32(12)},
    {"DefWindowProcW", u_DefWindowProcW, S32(4)},
    {"DefWindowProcA", u_DefWindowProcW, 0},
    {"DestroyCursor", u_True, 0},
    {"DestroyIcon", u_True, 0},
    {"DestroyWindow", u_DestroyWindow, S32(1)},
    {"DialogBoxParamW", u_MinusOne, S32(5)},
    {"DispatchMessageW", u_DispatchMessageW, S32(1)},
    {"DispatchMessageA", u_DispatchMessageW, 0},
    {"DisplayConfigGetDeviceInfo", u_NotSupported, 0},
    {"DragDetect", u_Zero, 0},
    {"EmptyClipboard", u_True, S32(0)},
    {"EndDialog", u_True, 0},
    {"EnumDisplayDevicesA", u_EnumDisplayDevicesA, 0},
    {"EnumDisplayMonitors", u_EnumDisplayMonitors, 0},
    {"EnumDisplaySettingsA", u_EnumDisplaySettingsA, 0},
    {"EnumDisplaySettingsW", u_EnumDisplaySettingsW, S32(3)},
    {"EnumWindows", u_EnumWindows, 0},
    {"GetActiveWindow", u_Main, S32(0)},
    {"GetAsyncKeyState", u_Zero, S32(1)},
    {"GetCaretBlinkTime", u_GetCaretBlinkTime, 0},
    {"GetClientRect", u_GetClientRect, S32(2)},
    {"GetClipboardData", u_Zero, S32(1)},
    {"GetCursorPos", u_GetCursorPos, S32(1)},
    {"GetDC", u_FakeDc, S32(1)},
    {"GetDesktopWindow", u_GetDesktopWindow, S32(0)},
    {"GetDisplayConfigBufferSizes", u_GetDisplayConfigBufferSizes, 0},
    {"GetDoubleClickTime", u_GetDoubleClickTime, 0},
    {"GetFocus", u_Main, S32(0)},
    {"GetForegroundWindow", u_Main, S32(0)},
    {"GetKeyboardLayout", u_GetKeyboardLayout, S32(1)},
    {"GetKeyboardLayoutNameW", u_GetKeyboardLayoutNameW, 0},
    {"GetKeyNameTextW", u_Zero, 0},
    {"GetKeyState", u_Zero, S32(1)},
    {"GetMessageExtraInfo", u_GetMessageExtraInfo, 0},
    {"GetMessageW", u_GetMessageW, S32(4)},
    {"GetMessageA", u_GetMessageW, 0},
    {"GetMonitorInfoA", u_GetMonitorInfoA, 0},
    {"GetMonitorInfoW", u_GetMonitorInfoW, S32(2)},
    {"GetParent", u_GetParent, S32(1)},
    {"GetRawInputBuffer", u_Zero, 0},
    {"GetRawInputData", u_MinusOne, 0},
    {"GetRawInputDeviceInfoW", u_GetRawInputDeviceInfoW, 0},
    {"GetRawInputDeviceInfoA", u_GetRawInputDeviceInfoA, 0},
    {"GetRawInputDeviceList", u_GetRawInputDeviceList, S32(3)},
    {"GetSystemMetrics", u_GetSystemMetrics, S32(1)},
    {"GetThreadDesktop", u_FakeIcon, 0},
    {"GetUserObjectInformationA", u_Zero, 0},
    {"GetWindow", u_Zero, S32(2)},
    {"GetWindowLongA", u_GetWindowLongA, 0},
    {"GetWindowLongW", u_GetWindowLongA, S32(2)},
    {"GetWindowLongPtrA", u_GetWindowLongPtrW, 0},
    {"GetWindowLongPtrW", u_GetWindowLongPtrW, 0},
    {"GetWindowPlacement", u_GetWindowPlacement, S32(2)},
    {"GetWindowRect", u_GetWindowRect, S32(2)},
    {"GetWindowThreadProcessId", u_GetWindowThreadProcessId, S32(2)},
    {"IsClipboardFormatAvailable", u_Zero, 0},
    {"IsIconic", u_Zero, S32(1)},
    {"IsWindowVisible", u_IsWindowVisible, S32(1)},
    {"KillTimer", u_KillTimer, S32(2)},
    {"LoadCursorA", u_FakeIcon, 0},
    {"LoadCursorW", u_FakeIcon, S32(2)},
    {"LoadIconA", u_FakeIcon, 0},
    {"LoadIconW", u_FakeIcon, S32(2)},
    {"LoadImageW", u_FakeIcon, S32(6)},
    {"MapVirtualKeyA", u_Zero, 0},
    {"MapVirtualKeyExA", u_Zero, 0},
    {"MapVirtualKeyW", u_Zero, S32(2)},
    {"MessageBoxA", u_MessageBoxA, 0},
    {"MonitorFromPoint", u_MonitorFrom, 0},
    {"MonitorFromRect", u_MonitorFrom, 0},
    {"MonitorFromWindow", u_MonitorFrom, S32(2)},
    {"MoveWindow", u_MoveWindow, S32(6)},
    {"MsgWaitForMultipleObjects", u_MsgWaitForMultipleObjects, S32(5)},
    {"OffsetRect", u_OffsetRect, 0},
    {"OpenClipboard", u_True, S32(1)},
    {"PeekMessageA", u_PeekMessageW, 0},
    {"PeekMessageW", u_PeekMessageW, S32(5)},
    {"PostMessageA", u_PostMessageW, 0},
    {"PostMessageW", u_PostMessageW, S32(4)},
    {"PostThreadMessageW", u_PostThreadMessageW, S32(4)},
    {"RegisterHotKey", u_True, S32(4)},
    {"UnregisterHotKey", u_True, S32(2)},
    {"PostQuitMessage", u_PostQuitMessage, S32(1)},
    {"PtInRect", u_PtInRect, 0},
    {"QueryDisplayConfig", u_QueryDisplayConfig, 0},
    {"RegisterClassExW", u_RegisterClassExW, S32(1)},
    {"RegisterClassW", u_RegisterClassW, S32(1)},
    {"RegisterDeviceNotificationW", u_FakeIcon, S32(3)},
    {"RegisterRawInputDevices", u_RegisterRawInputDevices, S32(3)},
    {"RegisterWindowMessageA", u_RegisterWindowMessageA, 0},
    {"ReleaseCapture", u_ReleaseCapture, S32(0)},
    {"ReleaseDC", u_True, S32(2)},
    {"ScreenToClient", u_ScreenToClient, S32(2)},
    {"SendDlgItemMessageW", u_Zero, 0},
    {"SendMessageA", u_SendMessageW, 0},
    {"SendMessageTimeoutA", u_SendMessageTimeoutA, 0},
    {"SendMessageW", u_SendMessageW, S32(4)},
    {"SetCapture", u_SetCapture, S32(1)},
    {"SetClipboardData", u_FakeIcon, S32(2)},
    {"SetCursor", u_Zero, S32(1)},
    {"SetCursorPos", u_SetCursorPos, S32(2)},
    {"SetDlgItemTextA", u_True, 0},
    {"SetDlgItemTextW", u_True, S32(3)},
    {"SetFocus", u_SetFocus, S32(1)},
    {"SetForegroundWindow", u_True, S32(1)},
    {"SetProcessDPIAware", u_True, 0},
    {"SetTimer", u_SetTimer, S32(4)},
    {"SetWindowLongA", u_SetWindowLongA, 0},
    {"SetWindowLongW", u_SetWindowLongA, S32(3)},
    {"SetWindowLongPtrA", u_SetWindowLongPtrW, 0},
    {"SetWindowLongPtrW", u_SetWindowLongPtrW, 0},
    {"SetWindowPos", u_SetWindowPos, S32(7)},
    {"SetWindowTextW", u_True, S32(2)},
    {"ShowCursor", u_ShowCursor, S32(1)},
    {"ShowWindow", u_ShowWindow, S32(2)},
    {"SystemParametersInfoW", u_SystemParametersInfoW, S32(4)},
    {"ToUnicode", u_Zero, S32(6)},
    {"TrackMouseEvent", u_True, 0},
    {"TranslateMessage", u_TranslateMessage, S32(1)},
    {"UnregisterClassW", u_UnregisterClassW, S32(2)},
    {"UnregisterDeviceNotification", u_True, S32(1)},
    {"UpdateWindow", u_True, S32(1)},
    {"ValidateRect", u_True, 0},
};
const size_t rcn_user32_x64_count = sizeof rcn_user32_x64 / sizeof rcn_user32_x64[0];
