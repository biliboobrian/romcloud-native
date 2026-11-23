// RomCloud Native : user32 (fenêtre, messages, boîte de dialogue) et winmm (horloge, minuteries),
// pour les jeux 32 bits.
//
// Le jeu n'a qu'une fenêtre, plein écran. Sa procédure reçoit à la création ce qu'un jeu attend pour
// se croire au premier plan (WM_CREATE, WM_ACTIVATEAPP, WM_ACTIVATE, WM_SETFOCUS), puis WM_CLOSE quand
// l'utilisateur demande à quitter. Il n'y a ni souris ni clavier : les entrées passent par DirectInput.
//
// Une boîte de dialogue (réglages de lancement) n'est pas affichée : sa procédure est appelée comme si
// l'utilisateur validait aussitôt (WM_INITDIALOG puis WM_COMMAND IDOK), avec les réglages qu'elle a
// elle-même proposés. Cases à cocher et listes gardent l'état que le jeu leur donne.
#include <stdlib.h>

#include "rcn_gui.h"

enum {
    WM_CREATE = 0x01,
    WM_DESTROY = 0x02,
    WM_ACTIVATE = 0x06,
    WM_SETFOCUS = 0x07,
    WM_CLOSE = 0x10,
    WM_QUIT = 0x12,
    WM_ACTIVATEAPP = 0x1C,
    WM_NCCREATE = 0x81,
    WM_INITDIALOG = 0x110,
    WM_COMMAND = 0x111,
    IDOK = 1,
    IDYES = 6,
    HANDLE_CONTROL = 0x00020000u, // GetDlgItem : HANDLE_CONTROL | identifiant
    HANDLE_WINDOW_DC = 0x00030001u,
    HANDLE_RESOURCE = 0x00040001u,
};

#define U32(n) ((u32)rcn_arg(ctx, n))

static u32 call_wndproc(RcnCtx *ctx, u32 message, u32 wparam, u32 lparam) {
    RcnGui *gui = rcn_gui(ctx);
    if (!gui->wndproc) return 0;
    u64 args[4] = {RCN_HWND, message, wparam, lparam};
    return (u32)rcn_call_guest_n(ctx, gui->wndproc, 4, args);
}

// Demande de l'utilisateur de quitter (manette) : un seul WM_CLOSE.
static void check_quit(RcnCtx *ctx) {
    static int asked;
    if (!asked && (rcn_pad(ctx) & RCN_PAD_QUIT)) {
        asked = 1;
        rcn_post_message(ctx, WM_CLOSE, 0, 0);
    }
}

// Retire (ou lit seulement) le premier message de la file. Renvoie 0 si elle est vide.
static int next_message(RcnCtx *ctx, u64 out, int remove) {
    RcnGui *gui = rcn_gui(ctx);
    RcnProcess *proc = ctx->proc;
    check_quit(ctx);
    pthread_mutex_lock(&proc->lock);
    int found = gui->queue_count > 0;
    if (found) {
        RcnMsg m = gui->queue[gui->queue_head];
        if (remove) {
            gui->queue_head = (gui->queue_head + 1) % 64;
            gui->queue_count--;
        }
        // MSG : hwnd, message, wParam, lParam, time, pt.x, pt.y
        memset(PTR(out), 0, 28);
        wr32(out, m.hwnd);
        wr32(out + 4, m.message);
        wr32(out + 8, m.wparam);
        wr32(out + 12, m.lparam);
        wr32(out + 16, (u32)((rcn_now_ns() - proc->start_ns) / 1000000));
    }
    pthread_mutex_unlock(&proc->lock);
    return found;
}

// --- Fenêtre ---

// RegisterClassExA(&class) : la procédure de fenêtre est le troisième champ.
static void u32_RegisterClassExA(RcnCtx *ctx) {
    rcn_gui(ctx)->wndproc = rd32(A0 + 8);
    RET(0xC001);
}
// CreateWindowExA(ex_style, class, name, style, x, y, width, height, parent, menu, instance, param)
static void u32_CreateWindowExA(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    // CREATESTRUCT : les mêmes arguments, dans l'ordre inverse.
    u32 *create = rcn_galloc(ctx, 48, 1);
    if (!create) {
        RET(0);
        return;
    }
    for (int i = 0; i < 12; i++) create[i] = U32(11 - i);
    u32 lparam = (u32)(uintptr_t)create;
    gui->window_created = 1;
    call_wndproc(ctx, WM_NCCREATE, 0, lparam);
    int refused = (i32)call_wndproc(ctx, WM_CREATE, 0, lparam) == -1;
    rcn_gfree(ctx, create);
    if (refused) {
        RET(0);
        return;
    }
    rcn_post_message(ctx, WM_ACTIVATEAPP, 1, 0);
    rcn_post_message(ctx, WM_ACTIVATE, 1, 0);
    rcn_post_message(ctx, WM_SETFOCUS, 0, 0);
    RET(RCN_HWND);
}
// DefWindowProcA(hwnd, message, wparam, lparam) : fermer la fenêtre la détruit.
static void u32_DefWindowProcA(RcnCtx *ctx) {
    u32 message = U32(1);
    if (message == WM_CLOSE) call_wndproc(ctx, WM_DESTROY, 0, 0);
    RET(message == WM_NCCREATE);
}
static void u32_DestroyWindow(RcnCtx *ctx) {
    if (U32(0) == RCN_HWND) call_wndproc(ctx, WM_DESTROY, 0, 0);
    RET(1);
}
static void u32_PostQuitMessage(RcnCtx *ctx) { rcn_post_message(ctx, WM_QUIT, U32(0), 0); }

// PeekMessageA(&msg, hwnd, min, max, remove)
static void u32_PeekMessageA(RcnCtx *ctx) { RET(next_message(ctx, A0, U32(4) & 1)); }
// GetMessageA(&msg, hwnd, min, max) : attend un message ; 0 pour WM_QUIT.
static void u32_GetMessageA(RcnCtx *ctx) {
    u64 out = A0;
    while (!next_message(ctx, out, 1)) rcn_sleep_ms(5);
    RET(rd32(out + 4) != WM_QUIT);
}
static void u32_WaitMessage(RcnCtx *ctx) {
    rcn_sleep_ms(5);
    RET(1);
}
static void u32_DispatchMessageA(RcnCtx *ctx) {
    u64 m = A0;
    RET(rd32(m) == RCN_HWND ? call_wndproc(ctx, rd32(m + 4), rd32(m + 8), rd32(m + 12)) : 0);
}
static void u32_Zero(RcnCtx *ctx) { RET(0); }
static void u32_One(RcnCtx *ctx) { RET(1); }

// GetWindowLongA / SetWindowLongA(hwnd, index[, value]) : GWL_WNDPROC (-4) est la procédure de fenêtre.
static u32 *window_long(RcnCtx *ctx, u32 hwnd, i32 index) {
    RcnGui *gui = rcn_gui(ctx);
    if (index == -4 && hwnd == RCN_HWND) return &gui->wndproc;
    return &gui->window_longs[hwnd == RCN_HWND ? 0 : 1][((u32)index / 4) & 7];
}
static void u32_GetWindowLongA(RcnCtx *ctx) { RET(*window_long(ctx, U32(0), (i32)U32(1))); }
static void u32_SetWindowLongA(RcnCtx *ctx) {
    u32 *slot = window_long(ctx, U32(0), (i32)U32(1));
    u32 old = *slot;
    *slot = U32(2);
    RET(old);
}

// GetWindowRect(hwnd, &rect) : la fenêtre couvre l'écran.
static void u32_GetWindowRect(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    u64 rect = A1;
    wr32(rect, 0);
    wr32(rect + 4, 0);
    wr32(rect + 8, gui->width);
    wr32(rect + 12, gui->height);
    RET(1);
}
// SetRect(&rect, left, top, right, bottom)
static void u32_SetRect(RcnCtx *ctx) {
    u64 rect = A0;
    for (int i = 0; i < 4; i++) wr32(rect + 4 * i, U32(1 + i));
    RET(1);
}
static void u32_GetCursorPos(RcnCtx *ctx) {
    wr32(A0, 0);
    wr32(A0 + 4, 0);
    RET(1);
}
static void u32_ShowCursor(RcnCtx *ctx) { RET(U32(0) ? 0 : (u32)-1); }
static void u32_GetDesktopWindow(RcnCtx *ctx) { RET(RCN_HWND_DESKTOP); }
static void u32_GetDC(RcnCtx *ctx) { RET(HANDLE_WINDOW_DC); }
static void u32_LoadResource(RcnCtx *ctx) { RET(HANDLE_RESOURCE); }
// MessageBoxA(hwnd, text, caption, type) : sur la sortie d'erreur ; répond OK, ou Oui à une question.
static void u32_MessageBoxA(RcnCtx *ctx) {
    const char *text = A1 ? STR(A1) : "", *caption = A2 ? STR(A2) : "";
    rcn_host_write(ctx, RCN_STREAM_STDERR, caption, strlen(caption));
    rcn_host_write(ctx, RCN_STREAM_STDERR, " : ", 3);
    rcn_host_write(ctx, RCN_STREAM_STDERR, text, strlen(text));
    rcn_host_write(ctx, RCN_STREAM_STDERR, "\n", 1);
    RET((U32(3) & 0xF) == 4 ? IDYES : IDOK);
}

// Fenêtre active (la nôtre), dernière fenêtre surgissante, informations d'objet : de quoi laisser Unity
// construire sa boîte d'erreur (elle interroge ces trois fonctions avant MessageBoxW).
static void u32_GetActiveWindow(RcnCtx *ctx) { RET(0x00020001); }
static void u32_GetLastActivePopup(RcnCtx *ctx) { RET(A0); }
// Station de fenêtres et bureau du processus : des handles imités, comme la fenêtre.
static void u32_GetProcessWindowStation(RcnCtx *ctx) { RET(0x00010001); }
static void u32_GetThreadDesktop(RcnCtx *ctx) { RET(0x00010002); }
static void u32_GetUserObjectInformationA(RcnCtx *ctx) {
    u32 index = (u32)A1;
    const char *text = index == 2 ? "RomCloud" : index == 6 ? "Window" : "";
    u32 size = (u32)A3, n = 0;
    if (A2 && size) {
        for (; text[n] && n + 1 < size; n++) wr8(A2 + (u64)n, (u8)text[n]);
        wr8(A2 + (u64)n, 0);
    }
    if (STK(4)) wr32(STK(4), n + 1);
    RET(1);
}
static void u32_GetUserObjectInformationW(RcnCtx *ctx) {
    u32 index = (u32)A1;
    const char *text = index == 2 ? "RomCloud" : index == 6 ? "Window" : "";
    u32 size = (u32)A3, n = 0;
    if (A2 && size) {
        for (; text[n] && (n + 1) * 2 <= size; n++) wr16(A2 + 2 * (u64)n, (u8)text[n]);
        if ((n + 1) * 2 <= size) wr16(A2 + 2 * (u64)n, 0);
    }
    if (STK(4)) wr32(STK(4), (n + 1) * 2);
    RET(1);
}

// MessageBoxW(hwnd, texte, titre, type) : la version Unicode, sur la sortie d'erreur (Unity s'en sert pour
// annoncer ses erreurs d'initialisation).
static void u32_MessageBoxW(RcnCtx *ctx) {
    char *text = A1 ? rcn_utf16_to_utf8(PTR(A1), -1) : NULL;
    char *caption = A2 ? rcn_utf16_to_utf8(PTR(A2), -1) : NULL;
    const char *t = text ? text : "", *c = caption ? caption : "";
    rcn_log("message : %s : %s", c, t);
    free(text);
    free(caption);
    RET((U32(3) & 0xF) == 4 ? IDYES : IDOK);
}

// --- Boîte de dialogue simulée ---

static RcnControl *control(RcnCtx *ctx, u32 id) {
    RcnGui *gui = rcn_gui(ctx);
    for (u32 i = 0; i < gui->control_count; i++) {
        if (gui->controls[i].id == id) return &gui->controls[i];
    }
    RcnControl *c = &gui->controls[gui->control_count < 127 ? gui->control_count++ : 127];
    memset(c, 0, sizeof *c);
    c->id = id;
    return c;
}
// DialogBoxParamA(instance, template, parent, procedure, param)
static void u32_DialogBoxParamA(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    u32 procedure = U32(3), param = U32(4);
    gui->control_count = 0;
    gui->dialog_ended = 0;
    gui->dialog_result = IDOK;
    u64 init[4] = {RCN_HWND_DIALOG, WM_INITDIALOG, 0, param};
    rcn_call_guest_n(ctx, procedure, 4, init);
    if (!gui->dialog_ended) {
        u64 ok[4] = {RCN_HWND_DIALOG, WM_COMMAND, IDOK, 0};
        rcn_call_guest_n(ctx, procedure, 4, ok);
    }
    RET(gui->dialog_result);
}
// EndDialog(dialog, result)
static void u32_EndDialog(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    gui->dialog_result = U32(1);
    gui->dialog_ended = 1;
    RET(1);
}
static void u32_GetDlgItem(RcnCtx *ctx) { RET(HANDLE_CONTROL | (U32(1) & 0xFFFF)); }
static void u32_IsDlgButtonChecked(RcnCtx *ctx) { RET(control(ctx, U32(1))->checked); }
static void u32_CheckDlgButton(RcnCtx *ctx) {
    control(ctx, U32(1))->checked = U32(2);
    RET(1);
}
// CheckRadioButton(dialog, first, last, checked)
static void u32_CheckRadioButton(RcnCtx *ctx) {
    u32 first = U32(1), last = U32(2), checked = U32(3);
    for (u32 id = first; id <= last && id - first < 64; id++) control(ctx, id)->checked = id == checked;
    RET(1);
}
// SendMessageA(hwnd, message, wparam, lparam) : fenêtre du jeu, ou contrôle d'une boîte de dialogue
// (cases à cocher, listes déroulantes).
static void u32_SendMessageA(RcnCtx *ctx) {
    u32 hwnd = U32(0), message = U32(1), wparam = U32(2), lparam = U32(3);
    if (hwnd == RCN_HWND) {
        RET(call_wndproc(ctx, message, wparam, lparam));
        return;
    }
    if ((hwnd & 0xFFFF0000u) != HANDLE_CONTROL) {
        RET(0);
        return;
    }
    RcnControl *c = control(ctx, hwnd & 0xFFFF);
    switch (message) {
    case 0x00F0: RET(c->checked); break;               // BM_GETCHECK
    case 0x00F1: c->checked = wparam; RET(0); break;    // BM_SETCHECK
    case 0x0143:                                        // CB_ADDSTRING
    case 0x0180: RET(c->items++); break;                // LB_ADDSTRING
    case 0x0146:                                        // CB_GETCOUNT
    case 0x018B: RET(c->items); break;                  // LB_GETCOUNT
    case 0x0147:                                        // CB_GETCURSEL
    case 0x0188: RET(c->selection); break;              // LB_GETCURSEL
    case 0x014E:                                        // CB_SETCURSEL
    case 0x0186: c->selection = wparam; RET(wparam); break; // LB_SETCURSEL
    case 0x014B:                                        // CB_RESETCONTENT
    case 0x0184: c->items = 0; c->selection = 0; RET(0); break; // LB_RESETCONTENT
    default: RET(0); break;
    }
}

// --- wsprintfA : formatage « C » (sans nombres à virgule, comme sous Windows) ---
// wsprintfA(tampon, format, ...) : %s %S %d %i %u %x %X %o %c %p %%, avec largeur et précision.
// wvsprintfA(tampon, format, va_list) : la même chose, arguments lus dans la liste (une case d'adresse
// chacun). from_list = 0 : arguments sur la pile, après le format.
static u32 ws_format(RcnCtx *ctx, u64 out, u64 fmt, int from_list, u64 va) {
    if (!out || !fmt) return 0;
    char *dst = PTR(out);
    const char *f = STR(fmt);
    u32 n = 0, arg = 2;
    for (const char *p = f; *p && n < 60000; p++) {
        if (*p != '%') {
            dst[n++] = *p;
            continue;
        }
        p++;
        if (*p == '%') {
            dst[n++] = '%';
            continue;
        }
        int left = 0, zero = 0, width = 0, prec = -1;
        while (*p == '-' || *p == '0' || *p == '+' || *p == ' ' || *p == '#') {
            if (*p == '-') left = 1;
            if (*p == '0') zero = 1;
            p++;
        }
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            prec = 0;
            p++;
            while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
        }
        while (*p == 'l' || *p == 'h' || *p == 'L' || *p == 'I' || *p == '6' || *p == '4') p++;
        char spec = *p, piece[1024];
        int pn = 0;
        u64 v = from_list ? rdp(ctx, va + PTR_SIZE(ctx) * (u64)(arg++ - 2)) : rcn_arg(ctx, (int)arg++);
        if (spec == 's' || spec == 'S') {
            char *text = NULL;
            if (spec == 's') {
                const char *s = v ? (const char *)PTR(v) : "(null)";
                pn = snprintf(piece, sizeof piece, "%.*s", prec >= 0 ? prec : 0x7fffffff, s);
            } else {
                text = v ? rcn_utf16_to_utf8(PTR(v), prec >= 0 ? prec : -1) : NULL;
                pn = snprintf(piece, sizeof piece, "%s", text ? text : "(null)");
                free(text);
            }
        } else if (spec == 'c' || spec == 'C') {
            pn = snprintf(piece, sizeof piece, "%c", spec == 'c' ? (int)(u8)v : (int)(u16)v);
        } else if (spec && strchr("diuxXop", spec)) {
            char ff[24];
            int fl = 0;
            ff[fl++] = '%';
            if (left) ff[fl++] = '-';
            if (zero && !left) ff[fl++] = '0';
            if (width) fl += snprintf(ff + fl, sizeof ff - fl, "%d", width);
            if (prec >= 0) fl += snprintf(ff + fl, sizeof ff - fl, ".%d", prec);
            if (spec == 'd' || spec == 'i') ff[fl++] = 'd';
            else if (spec == 'u') ff[fl++] = 'u';
            else if (spec == 'o') ff[fl++] = 'o';
            else if (spec == 'X') ff[fl++] = 'X';
            else ff[fl++] = 'x';
            ff[fl] = 0;
            if (spec == 'd' || spec == 'i') pn = snprintf(piece, sizeof piece, ff, (int)(i32)v);
            else pn = snprintf(piece, sizeof piece, ff, (unsigned)v);
        } else {
            dst[n++] = '%';
            if (spec) dst[n++] = spec;
            continue;
        }
        if (pn < 0) pn = 0;
        if (pn > (int)sizeof piece - 1) pn = (int)sizeof piece - 1;
        int pad = width > pn && (spec == 's' || spec == 'S' || spec == 'c' || spec == 'C') ? width - pn : 0;
        if (pad && !left) {
            memset(dst + n, ' ', (size_t)pad);
            n += (u32)pad;
        }
        memcpy(dst + n, piece, (size_t)pn);
        n += (u32)pn;
        if (pad && left) {
            memset(dst + n, ' ', (size_t)pad);
            n += (u32)pad;
        }
    }
    dst[n] = 0;
    return n;
}
static void u32_wsprintfA(RcnCtx *ctx) { RET(ws_format(ctx, A0, A1, 0, 0)); }
static void u32_wvsprintfA(RcnCtx *ctx) { RET(ws_format(ctx, A0, A1, 1, A2)); }

const RcnShim rcn_user32[] = {
    {"wsprintfA", u32_wsprintfA, C32},
    {"wvsprintfA", u32_wvsprintfA, S32(3)},
    {"AdjustWindowRectEx", u32_One, S32(4)},
    {"CheckDlgButton", u32_CheckDlgButton, S32(3)},
    {"CheckRadioButton", u32_CheckRadioButton, S32(4)},
    {"ClientToScreen", u32_One, S32(2)},
    {"ClipCursor", u32_One, S32(1)},
    {"CreateWindowExA", u32_CreateWindowExA, S32(12)},
    {"DefWindowProcA", u32_DefWindowProcA, S32(4)},
    {"DestroyWindow", u32_DestroyWindow, S32(1)},
    {"DialogBoxParamA", u32_DialogBoxParamA, S32(5)},
    {"DispatchMessageA", u32_DispatchMessageA, S32(1)},
    {"EnableWindow", u32_Zero, S32(2)},
    {"EndDialog", u32_EndDialog, S32(2)},
    {"GetCursorPos", u32_GetCursorPos, S32(1)},
    {"GetDC", u32_GetDC, S32(1)},
    {"GetDesktopWindow", u32_GetDesktopWindow, S32(0)},
    {"GetDlgItem", u32_GetDlgItem, S32(2)},
    {"GetMessageA", u32_GetMessageA, S32(4)},
    {"GetWindowLongA", u32_GetWindowLongA, S32(2)},
    {"GetWindowRect", u32_GetWindowRect, S32(2)},
    {"IsDialogMessageA", u32_Zero, S32(2)},
    {"IsDlgButtonChecked", u32_IsDlgButtonChecked, S32(2)},
    {"LoadCursorA", u32_LoadResource, S32(2)},
    {"LoadIconA", u32_LoadResource, S32(2)},
    {"MessageBoxA", u32_MessageBoxA, S32(4)}, {"MessageBoxW", u32_MessageBoxW, S32(4)},
    {"GetActiveWindow", u32_GetActiveWindow, S32(0)},
    {"GetLastActivePopup", u32_GetLastActivePopup, S32(1)},
    {"GetUserObjectInformationW", u32_GetUserObjectInformationW, S32(5)},
    {"GetUserObjectInformationA", u32_GetUserObjectInformationA, S32(5)},
    {"GetProcessWindowStation", u32_GetProcessWindowStation, S32(0)},
    {"GetThreadDesktop", u32_GetThreadDesktop, S32(1)},
    {"PeekMessageA", u32_PeekMessageA, S32(5)},
    {"PostQuitMessage", u32_PostQuitMessage, S32(1)},
    {"RegisterClassExA", u32_RegisterClassExA, S32(1)},
    {"ReleaseCapture", u32_One, S32(0)},
    {"ReleaseDC", u32_One, S32(2)},
    {"ScreenToClient", u32_One, S32(2)},
    {"SendMessageA", u32_SendMessageA, S32(4)},
    {"SetCapture", u32_Zero, S32(1)},
    {"SetCursorPos", u32_One, S32(2)},
    {"SetProcessDPIAware", u32_One, 0},
    {"SetRect", u32_SetRect, S32(5)},
    {"SetWindowLongA", u32_SetWindowLongA, S32(3)},
    {"ShowCursor", u32_ShowCursor, S32(1)},
    {"TranslateMessage", u32_Zero, S32(1)},
    {"WaitMessage", u32_WaitMessage, S32(0)},
};
const size_t rcn_user32_count = sizeof rcn_user32 / sizeof rcn_user32[0];

// --- winmm : horloge en millisecondes et minuteries ---

static void mm_timeGetTime(RcnCtx *ctx) { RET((u32)((rcn_now_ns() - ctx->proc->start_ns) / 1000000)); }
// timeGetDevCaps(&caps, size) : périodes de 1 ms à 1000 s.
static void mm_timeGetDevCaps(RcnCtx *ctx) {
    wr32(A0, 1);
    wr32(A0 + 4, 1000000);
    RET(0);
}

// Minuterie : un thread du jeu dont la fonction d'entrée est une fonction de l'hôte (timer_loop), qui
// appelle la fonction du jeu à chaque période.
typedef struct {
    int active;
    u32 delay, callback, user, periodic;
} Timer;
static Timer timers[16];

static void timer_loop(RcnCtx *ctx) {
    u32 id = U32(0);
    Timer *timer = &timers[id - 1];
    u64 next = rcn_now_ns();
    while (__atomic_load_n(&timer->active, __ATOMIC_SEQ_CST)) {
        next += (u64)timer->delay * 1000000;
        u64 now = rcn_now_ns();
        if (now < next) rcn_sleep_ms((u32)((next - now) / 1000000));
        else if (now - next > 500000000ull) next = now; // trop de retard : on repart de maintenant
        if (!__atomic_load_n(&timer->active, __ATOMIC_SEQ_CST)) break;
        // Fonction du jeu : (identifiant, message, données, réservé, réservé).
        u64 args[5] = {id, 0, timer->user, 0, 0};
        rcn_call_guest_n(ctx, timer->callback, 5, args);
        if (!timer->periodic) __atomic_store_n(&timer->active, 0, __ATOMIC_SEQ_CST);
    }
    RET(0);
}
// timeSetEvent(delay, resolution, callback, user, flags) : renvoie un identifiant, 0 en cas d'échec.
static void mm_timeSetEvent(RcnCtx *ctx) {
    static u64 loop_thunk;
    RcnProcess *proc = ctx->proc;
    u32 delay = U32(0), callback = U32(2), user = U32(3), flags = U32(4);
    u32 id = 0;
    pthread_mutex_lock(&proc->lock);
    for (u32 i = 0; i < 16 && !id; i++) {
        if (!timers[i].active) id = i + 1;
    }
    if (id) {
        Timer *timer = &timers[id - 1];
        timer->delay = delay ? delay : 1;
        timer->callback = callback;
        timer->user = user;
        timer->periodic = flags & 1; // TIME_PERIODIC
        timer->active = 1;
    }
    pthread_mutex_unlock(&proc->lock);
    if (id && !loop_thunk) loop_thunk = rcn_thunk(ctx, "winmm", "minuterie", timer_loop, 0);
    RcnThread *thread = id && loop_thunk ? rcn_thread_new(proc, 0) : NULL;
    if (!thread || rcn_thread_start(thread, loop_thunk, id) != 0) {
        if (id) timers[id - 1].active = 0;
        RET(0);
        return;
    }
    RET(id);
}
static void mm_timeKillEvent(RcnCtx *ctx) {
    u32 id = U32(0);
    if (id >= 1 && id <= 16) __atomic_store_n(&timers[id - 1].active, 0, __ATOMIC_SEQ_CST);
    RET(0);
}

const RcnShim rcn_winmm[] = {
    {"timeBeginPeriod", u32_Zero, S32(1)},
    {"timeEndPeriod", u32_Zero, S32(1)},
    {"timeGetDevCaps", mm_timeGetDevCaps, S32(2)},
    {"timeGetTime", mm_timeGetTime, S32(0)},
    {"timeKillEvent", mm_timeKillEvent, S32(1)},
    {"timeSetEvent", mm_timeSetEvent, S32(5)},
};
const size_t rcn_winmm_count = sizeof rcn_winmm / sizeof rcn_winmm[0];
