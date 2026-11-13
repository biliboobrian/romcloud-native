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
    SCREEN_W = 1920, SCREEN_H = 1080,
    MONITOR = 0x30001, DESKTOP = 0x10010, FAKE_DC = 0x30010, FAKE_ICON = 0x40001,
};

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
static int take(RcnCtx *ctx, u64 out, u64 hwnd, u32 min, u32 max, int remove) {
    // Fermeture demandée par l'utilisateur : WM_CLOSE à la fenêtre principale.
    if (!us.quit_posted && us.main && (rcn_pad_buttons(ctx) & RCN_PAD_QUIT)) {
        us.quit_posted = 1;
        post(us.main, WM_CLOSE, 0, 0);
    }
    pthread_mutex_lock(&us.lock);
    fire_timers_locked();
    int found = 0;
    for (u32 k = 0; k < us.count; k++) {
        u32 at = (us.head + k) % QUEUE_SIZE;
        Msg m = us.queue[at];
        if (!matches(&m, hwnd, min, max)) continue;
        found = 1;
        if (out) {
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
static void u_RegisterClassExW(RcnCtx *ctx) { RET(add_class(ctx, rd64(A0 + 64), rd64(A0 + 8), rd32(A0 + 20))); }
static void u_RegisterClassW(RcnCtx *ctx) { RET(add_class(ctx, rd64(A0 + 64), rd64(A0 + 8), rd32(A0 + 20))); }
static void u_UnregisterClassW(RcnCtx *ctx) { RET(1); }

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
    u8 *cs = calloc(1, 80);
    if (cs) {
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
        u64 hwnd = win->hwnd;
        if (!call_proc(ctx, hwnd, WM_NCCREATE, 0, p) || (i64)call_proc(ctx, hwnd, WM_CREATE, 0, p) == -1) {
            win->used = 0;
            if (us.main == hwnd) us.main = 0;
            free(cs);
            RET(0);
            return;
        }
        free(cs);
    }
    if (win->style & 0x10000000) win->visible = 1; // WS_VISIBLE
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
    }
    RET(previous);
}
static void resize(RcnCtx *ctx, Window *win, i32 x, i32 y, i32 w, i32 h, int move, int size) {
    if (move) {
        win->x = x;
        win->y = y;
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
    RET(rd32(out + 8) != WM_QUIT);
}
static void u_PostMessageW(RcnCtx *ctx) {
    post(A0, (u32)A1, A2, A3);
    RET(1);
}
static void u_PostQuitMessage(RcnCtx *ctx) { post(0, WM_QUIT, (u32)A0, 0); }
static void u_TranslateMessage(RcnCtx *ctx) { RET(0); }
static void u_DispatchMessageW(RcnCtx *ctx) {
    u64 m = A0, hwnd = rd64(m), w = rd64(m + 16), l = rd64(m + 24);
    u32 message = rd32(m + 8);
    if (message == WM_TIMER && l) {
        RET(rcn_call_guest(ctx, l, hwnd, WM_TIMER, w, (u32)(rcn_now_ns() / 1000000)));
        return;
    }
    RET(call_proc(ctx, hwnd, message, w, l));
}
static void u_SendMessageW(RcnCtx *ctx) { RET(call_proc(ctx, A0, (u32)A1, A2, A3)); }
// SendMessageTimeoutA(hwnd, message, w, l, flags, timeout, &result)
static void u_SendMessageTimeoutA(RcnCtx *ctx) {
    u64 r = window_of(A0) ? call_proc(ctx, A0, (u32)A1, A2, A3) : 0;
    if (STK(6)) wr64(STK(6), r);
    RET(1);
}
// MsgWaitForMultipleObjects(count, handles, all, milliseconds, mask)
static void u_MsgWaitForMultipleObjects(RcnCtx *ctx) {
    u32 count = (u32)A0, ms = (u32)A3;
    u64 handles[64];
    for (u32 i = 0; i < count && i < 64; i++) handles[i] = rd64(A1 + 8 * (u64)i);
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
// EnumDisplayDevicesA(device, index, &DISPLAY_DEVICEA, flags) : un écran.
static void u_EnumDisplayDevicesA(RcnCtx *ctx) {
    if ((u32)A1 != 0) {
        RET(0);
        return;
    }
    u64 d = A2;
    memset(PTR(d + 4), 0, 420);
    strcpy(PTR(d + 4), "\\\\.\\DISPLAY1");
    strcpy(PTR(d + 36), "RomCloud");
    wr32(d + 164, 0x5); // DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | PRIMARY_DEVICE
    RET(1);
}
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
static void u_GetCursorPos(RcnCtx *ctx) {
    wr32(A0, SCREEN_W / 2);
    wr32(A0 + 4, SCREEN_H / 2);
    RET(1);
}
static void u_ShowCursor(RcnCtx *ctx) { RET_I32((u32)A0 ? ++us.cursor_count : --us.cursor_count); }
// GetRawInputDeviceList(list, &count, size) : aucun périphérique.
static void u_GetRawInputDeviceList(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(0);
}
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

const RcnShim rcn_user32_x64[] = {
    {"AdjustWindowRectEx", u_AdjustWindowRectEx, 0},
    {"AllowSetForegroundWindow", u_True, 0},
    {"ClientToScreen", u_ClientToScreen, 0},
    {"ClipCursor", u_True, 0},
    {"CloseClipboard", u_True, 0},
    {"CopyRect", u_CopyRect, 0},
    {"CreateWindowExW", u_CreateWindowExW, 0},
    {"DefWindowProcW", u_DefWindowProcW, 0},
    {"DefWindowProcA", u_DefWindowProcW, 0},
    {"DestroyCursor", u_True, 0},
    {"DestroyIcon", u_True, 0},
    {"DestroyWindow", u_DestroyWindow, 0},
    {"DialogBoxParamW", u_MinusOne, 0},
    {"DispatchMessageW", u_DispatchMessageW, 0},
    {"DispatchMessageA", u_DispatchMessageW, 0},
    {"DisplayConfigGetDeviceInfo", u_NotSupported, 0},
    {"DragDetect", u_Zero, 0},
    {"EmptyClipboard", u_True, 0},
    {"EndDialog", u_True, 0},
    {"EnumDisplayDevicesA", u_EnumDisplayDevicesA, 0},
    {"EnumDisplayMonitors", u_EnumDisplayMonitors, 0},
    {"EnumDisplaySettingsA", u_EnumDisplaySettingsA, 0},
    {"EnumDisplaySettingsW", u_EnumDisplaySettingsW, 0},
    {"EnumWindows", u_EnumWindows, 0},
    {"GetActiveWindow", u_Main, 0},
    {"GetAsyncKeyState", u_Zero, 0},
    {"GetCaretBlinkTime", u_GetCaretBlinkTime, 0},
    {"GetClientRect", u_GetClientRect, 0},
    {"GetClipboardData", u_Zero, 0},
    {"GetCursorPos", u_GetCursorPos, 0},
    {"GetDC", u_FakeDc, 0},
    {"GetDesktopWindow", u_GetDesktopWindow, 0},
    {"GetDisplayConfigBufferSizes", u_NotSupported, 0},
    {"GetDoubleClickTime", u_GetDoubleClickTime, 0},
    {"GetFocus", u_Main, 0},
    {"GetForegroundWindow", u_Main, 0},
    {"GetKeyboardLayout", u_GetKeyboardLayout, 0},
    {"GetKeyboardLayoutNameW", u_GetKeyboardLayoutNameW, 0},
    {"GetKeyNameTextW", u_Zero, 0},
    {"GetKeyState", u_Zero, 0},
    {"GetMessageExtraInfo", u_GetMessageExtraInfo, 0},
    {"GetMessageW", u_GetMessageW, 0},
    {"GetMessageA", u_GetMessageW, 0},
    {"GetMonitorInfoA", u_GetMonitorInfoA, 0},
    {"GetMonitorInfoW", u_GetMonitorInfoW, 0},
    {"GetParent", u_GetParent, 0},
    {"GetRawInputBuffer", u_Zero, 0},
    {"GetRawInputData", u_MinusOne, 0},
    {"GetRawInputDeviceInfoW", u_MinusOne, 0},
    {"GetRawInputDeviceList", u_GetRawInputDeviceList, 0},
    {"GetSystemMetrics", u_GetSystemMetrics, 0},
    {"GetThreadDesktop", u_FakeIcon, 0},
    {"GetUserObjectInformationA", u_Zero, 0},
    {"GetWindow", u_Zero, 0},
    {"GetWindowLongA", u_GetWindowLongA, 0},
    {"GetWindowLongW", u_GetWindowLongA, 0},
    {"GetWindowLongPtrA", u_GetWindowLongPtrW, 0},
    {"GetWindowLongPtrW", u_GetWindowLongPtrW, 0},
    {"GetWindowPlacement", u_GetWindowPlacement, 0},
    {"GetWindowRect", u_GetWindowRect, 0},
    {"GetWindowThreadProcessId", u_GetWindowThreadProcessId, 0},
    {"IsClipboardFormatAvailable", u_Zero, 0},
    {"IsIconic", u_Zero, 0},
    {"IsWindowVisible", u_IsWindowVisible, 0},
    {"KillTimer", u_KillTimer, 0},
    {"LoadCursorA", u_FakeIcon, 0},
    {"LoadCursorW", u_FakeIcon, 0},
    {"LoadIconA", u_FakeIcon, 0},
    {"LoadIconW", u_FakeIcon, 0},
    {"LoadImageW", u_FakeIcon, 0},
    {"MapVirtualKeyA", u_Zero, 0},
    {"MapVirtualKeyExA", u_Zero, 0},
    {"MapVirtualKeyW", u_Zero, 0},
    {"MessageBoxA", u_MessageBoxA, 0},
    {"MonitorFromPoint", u_MonitorFrom, 0},
    {"MonitorFromRect", u_MonitorFrom, 0},
    {"MonitorFromWindow", u_MonitorFrom, 0},
    {"MoveWindow", u_MoveWindow, 0},
    {"MsgWaitForMultipleObjects", u_MsgWaitForMultipleObjects, 0},
    {"OffsetRect", u_OffsetRect, 0},
    {"OpenClipboard", u_True, 0},
    {"PeekMessageA", u_PeekMessageW, 0},
    {"PeekMessageW", u_PeekMessageW, 0},
    {"PostMessageA", u_PostMessageW, 0},
    {"PostMessageW", u_PostMessageW, 0},
    {"PostQuitMessage", u_PostQuitMessage, 0},
    {"PtInRect", u_PtInRect, 0},
    {"QueryDisplayConfig", u_NotSupported, 0},
    {"RegisterClassExW", u_RegisterClassExW, 0},
    {"RegisterClassW", u_RegisterClassW, 0},
    {"RegisterDeviceNotificationW", u_FakeIcon, 0},
    {"RegisterRawInputDevices", u_True, 0},
    {"RegisterWindowMessageA", u_RegisterWindowMessageA, 0},
    {"ReleaseCapture", u_True, 0},
    {"ReleaseDC", u_True, 0},
    {"ScreenToClient", u_ScreenToClient, 0},
    {"SendDlgItemMessageW", u_Zero, 0},
    {"SendMessageA", u_SendMessageW, 0},
    {"SendMessageTimeoutA", u_SendMessageTimeoutA, 0},
    {"SendMessageW", u_SendMessageW, 0},
    {"SetCapture", u_Zero, 0},
    {"SetClipboardData", u_FakeIcon, 0},
    {"SetCursor", u_Zero, 0},
    {"SetCursorPos", u_True, 0},
    {"SetDlgItemTextA", u_True, 0},
    {"SetDlgItemTextW", u_True, 0},
    {"SetFocus", u_SetFocus, 0},
    {"SetForegroundWindow", u_True, 0},
    {"SetProcessDPIAware", u_True, 0},
    {"SetTimer", u_SetTimer, 0},
    {"SetWindowLongA", u_SetWindowLongA, 0},
    {"SetWindowLongW", u_SetWindowLongA, 0},
    {"SetWindowLongPtrA", u_SetWindowLongPtrW, 0},
    {"SetWindowLongPtrW", u_SetWindowLongPtrW, 0},
    {"SetWindowPos", u_SetWindowPos, 0},
    {"SetWindowTextW", u_True, 0},
    {"ShowCursor", u_ShowCursor, 0},
    {"ShowWindow", u_ShowWindow, 0},
    {"SystemParametersInfoW", u_SystemParametersInfoW, 0},
    {"ToUnicode", u_Zero, 0},
    {"TrackMouseEvent", u_True, 0},
    {"TranslateMessage", u_TranslateMessage, 0},
    {"UnregisterClassW", u_UnregisterClassW, 0},
    {"UnregisterDeviceNotification", u_True, 0},
    {"UpdateWindow", u_True, 0},
    {"ValidateRect", u_True, 0},
};
const size_t rcn_user32_x64_count = sizeof rcn_user32_x64 / sizeof rcn_user32_x64[0];
