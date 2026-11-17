// RomCloud Native : fenêtre du jeu, file de messages, affichage et manette côté hôte.
//
// Hôte par défaut (lanceur en ligne de commande), pour vérifier le rendu sans écran :
//   RCN_FRAMES=<dossier>      enregistre des images du jeu (frame-000060.bmp…)
//   RCN_FRAME_EVERY=<n>       une image sur n (60 par défaut)
//   RCN_MAX_FRAMES=<n>        arrête le jeu après n images
//   RCN_AUDIO=<fichier.wav>   enregistre le son du jeu (sans ce réglage, pas de périphérique de son)
//   RCN_PAD_SCRIPT=<liste>    manette simulée : « image:boutons » en hexadécimal, séparés par des
//                             virgules (ex. « 120:10,130:0 » : bouton A de l'image 120 à 129)
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_gui.h"

RcnGui *rcn_gui(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    if (!proc->gui) {
        proc->gui = calloc(1, sizeof(RcnGui));
        if (proc->gui) {
            proc->gui->width = 640;
            proc->gui->height = 480;
            proc->gui->bpp = 32;
        }
    }
    RcnGui *gui = proc->gui;
    pthread_mutex_unlock(&proc->lock);
    if (!gui) rcn_fail(ctx, "memoire insuffisante");
    return gui;
}

void rcn_post_message(RcnCtx *ctx, u32 message, u32 wparam, u32 lparam) {
    RcnGui *gui = rcn_gui(ctx);
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    if (gui->queue_count < 64) {
        RcnMsg *m = &gui->queue[(gui->queue_head + gui->queue_count++) % 64];
        m->hwnd = RCN_HWND;
        m->message = message;
        m->wparam = wparam;
        m->lparam = lparam;
    }
    pthread_mutex_unlock(&proc->lock);
}

// Compteur d'images de l'hôte par défaut (un seul jeu par processus).
static u64 default_frames;

void rcn_present(RcnCtx *ctx, const void *pixels, u32 width, u32 height, u32 pitch, u32 bpp, int wait) {
    RcnGui *gui = rcn_gui(ctx);
    const RcnHost *host = ctx->proc->host;
    if (host->present) {
        host->present(host->user, pixels, (int)width, (int)height, (int)pitch, bpp == 16 ? RCN_PIXEL_RGB565 : RCN_PIXEL_XRGB8888);
    }
    gui->frames++;
    // Cadence mesurée, notée toutes les 5 secondes.
    static u64 window_start, window_frames;
    u64 t = rcn_now_ns();
    if (!window_start) window_start = t;
    window_frames++;
    if (t - window_start >= 5000000000ull) {
        rcn_log("rcn : %.1f images/s (%ux%u)", window_frames * 1e9 / (double)(t - window_start), width, height);
        window_start = t;
        window_frames = 0;
    }    // Synchronisation verticale simulée : 60 images par seconde au plus.
    if (wait) {
        const u64 period = 1000000000ull / 60;
        u64 now = rcn_now_ns();
        u64 next = gui->last_present_ns + period;
        if (now < next) {
            rcn_sleep_ms((u32)((next - now) / 1000000));
            now = rcn_now_ns();
        }
        gui->last_present_ns = now > next + period ? now : next;
    }
    const char *max = host->present == rcn_default_present ? getenv("RCN_MAX_FRAMES") : NULL;
    if (max && gui->frames >= (u64)atoll(max)) rcn_exit(ctx, 0);
}

u32 rcn_pad_buttons(RcnCtx *ctx) {
    const RcnHost *host = ctx->proc->host;
    return host->pad ? host->pad(host->user) : 0;
}

void rcn_pad_axes(RcnCtx *ctx, i16 axes[6]) {
    const RcnHost *host = ctx->proc->host;
    memset(axes, 0, 6 * sizeof *axes);
    if (host->pad_axes) host->pad_axes(host->user, axes);
    // Gâchettes tout-ou-rien (boutons L2 / R2) : à fond.
    u32 buttons = rcn_pad_buttons(ctx);
    if ((buttons & RCN_PAD_LT) && axes[4] == 0) axes[4] = 32767;
    if ((buttons & RCN_PAD_RT) && axes[5] == 0) axes[5] = 32767;
}

// Jeux à croix seulement (DirectInput, clavier) : le stick gauche donne aussi des directions.
u32 rcn_pad(RcnCtx *ctx) {
    u32 buttons = rcn_pad_buttons(ctx);
    i16 axes[6];
    rcn_pad_axes(ctx, axes);
    const i16 threshold = 16384;
    if (axes[0] < -threshold) buttons |= RCN_PAD_LEFT;
    if (axes[0] > threshold) buttons |= RCN_PAD_RIGHT;
    if (axes[1] < -threshold) buttons |= RCN_PAD_UP;
    if (axes[1] > threshold) buttons |= RCN_PAD_DOWN;
    return buttons;
}

// --- Hôte par défaut ---

static void put32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

// Enregistre l'image en BMP 24 bits.
static void write_bmp(const char *path, const u8 *pixels, int width, int height, int pitch, int format) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int row = (width * 3 + 3) & ~3;
    u8 header[54] = {'B', 'M'};
    put32(header + 2, 54 + (u32)(row * height));
    put32(header + 10, 54);
    put32(header + 14, 40);
    put32(header + 18, (u32)width);
    put32(header + 22, (u32)height);
    header[26] = 1;
    header[28] = 24;
    fwrite(header, 1, 54, f);
    u8 *line = calloc(1, (size_t)row);
    for (int y = height - 1; line && y >= 0; y--) { // lignes de bas en haut
        const u8 *src = pixels + (size_t)y * (size_t)pitch;
        for (int x = 0; x < width; x++) {
            u8 r, g, b;
            if (format == RCN_PIXEL_RGB565) {
                u16 p = (u16)(src[x * 2] | src[x * 2 + 1] << 8);
                r = (u8)((p >> 11) << 3);
                g = (u8)(((p >> 5) & 63) << 2);
                b = (u8)((p & 31) << 3);
            } else {
                b = src[x * 4];
                g = src[x * 4 + 1];
                r = src[x * 4 + 2];
            }
            line[x * 3] = b;
            line[x * 3 + 1] = g;
            line[x * 3 + 2] = r;
        }
        fwrite(line, 1, (size_t)row, f);
    }
    free(line);
    fclose(f);
}

void rcn_default_present(void *user, const void *pixels, int width, int height, int pitch, int format) {
    (void)user;
    u64 frame = ++default_frames;
    const char *dir = getenv("RCN_FRAMES");
    if (!dir || !pixels) return;
    const char *every = getenv("RCN_FRAME_EVERY");
    u64 step = every ? (u64)atoll(every) : 60;
    if (step == 0 || frame % step != 0) return;
    char path[512];
    snprintf(path, sizeof path, "%s/frame-%06llu.bmp", dir, (unsigned long long)frame);
    write_bmp(path, pixels, width, height, pitch, format);
}

uint32_t rcn_default_pad(void *user) {
    (void)user;
    // Scénario lu une fois (« image:boutons » en hexadécimal, séparés par des virgules, images croissantes) ;
    // à chaque appel, entrée en vigueur à l'image courante (curseur qui avance).
    static struct {
        u64 frame;
        uint32_t buttons;
    } *steps;
    static size_t count, at;
    static int parsed;
    if (!parsed) {
        parsed = 1;
        const char *script = getenv("RCN_PAD_SCRIPT");
        size_t cap = 0;
        while (script && *script) {
            char *end;
            u64 frame = strtoull(script, &end, 10);
            if (*end != ':') break;
            uint32_t value = (uint32_t)strtoul(end + 1, &end, 16);
            if (count == cap) {
                cap = cap ? cap * 2 : 64;
                void *grown = realloc(steps, cap * sizeof *steps);
                if (!grown) break;
                steps = grown;
            }
            steps[count].frame = frame, steps[count].buttons = value, count++;
            if (*end != ',') break;
            script = end + 1;
        }
    }
    // RCN_KEY_SCRIPT=<image:touche:enfoncée,…> : frappes simulées (touches virtuelles de Windows en
    // hexadécimal, 1 enfoncée / 0 relâchée), par le clavier de l'hôte comme sur le téléphone.
    static struct {
        u64 frame;
        int vk, down;
    } keys[64];
    static size_t key_count, key_at;
    static int keys_parsed;
    if (!keys_parsed) {
        keys_parsed = 1;
        const char *script = getenv("RCN_KEY_SCRIPT");
        while (script && *script && key_count < 64) {
            char *end;
            u64 frame = strtoull(script, &end, 10);
            if (*end != ':') break;
            int vk = (int)strtol(end + 1, &end, 16);
            if (*end != ':') break;
            int down = (int)strtol(end + 1, &end, 10);
            keys[key_count].frame = frame, keys[key_count].vk = vk, keys[key_count].down = down, key_count++;
            if (*end != ',') break;
            script = end + 1;
        }
    }
    while (key_at < key_count && keys[key_at].frame <= default_frames) {
        void rcn_keyboard_key(int vk, int down);
        void rcn_user32_mouse(int x, int y, int down);
        // Touche 1 (VK_LBUTTON) : clic gauche en RCN_MOUSE_POS=x,y (écran de 1920 × 1080).
        if (keys[key_at].vk == 1) {
            int x = 960, y = 540;
            const char *pos = getenv("RCN_MOUSE_POS");
            if (pos) sscanf(pos, "%d,%d", &x, &y);
            rcn_user32_mouse(x, y, keys[key_at].down);
        } else {
            rcn_keyboard_key(keys[key_at].vk, keys[key_at].down);
        }
        key_at++;
    }
    while (at + 1 < count && steps[at + 1].frame <= default_frames) at++;
    return count && steps[at].frame <= default_frames ? steps[at].buttons : 0;
}

// Son : un thread tire les échantillons au rythme réel et les ajoute à un fichier WAV. Le jeu se
// terminant sans prévenir, l'en-tête annonce d'emblée une taille maximale.
static struct {
    FILE *file;
    RcnAudioFill fill;
    void *context;
    int per_10ms; // rames par 10 ms
} audio_dump;

static void *audio_dump_loop(void *arg) {
    (void)arg;
    int16_t block[960 * 2]; // 10 ms jusqu'à 96 kHz
    int n = audio_dump.per_10ms;
    u64 next = rcn_now_ns();
    for (;;) {
        audio_dump.fill(audio_dump.context, block, n);
        fwrite(block, sizeof(int16_t) * 2, (size_t)n, audio_dump.file);
        fflush(audio_dump.file);
        next += 10000000ull;
        u64 now = rcn_now_ns();
        if (now < next) rcn_sleep_ms((u32)((next - now) / 1000000));
    }
    return NULL;
}

int rcn_default_audio_open(void *user, int sample_rate, RcnAudioFill fill, void *context) {
    (void)user;
    const char *path = getenv("RCN_AUDIO");
    if (!path || audio_dump.file || sample_rate <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    u8 header[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    put32(header + 4, 0x7FFFFFF0u);
    put32(header + 16, 16);
    header[20] = 1; // PCM
    header[22] = 2; // stéréo
    put32(header + 24, (u32)sample_rate);
    put32(header + 28, (u32)sample_rate * 4);
    header[32] = 4;
    header[34] = 16;
    memcpy(header + 36, "data", 4);
    put32(header + 40, 0x7FFFFF00u);
    fwrite(header, 1, sizeof header, f);
    audio_dump.file = f;
    audio_dump.fill = fill;
    audio_dump.context = context;
    audio_dump.per_10ms = sample_rate / 100 > 960 ? 960 : sample_rate / 100;
    pthread_t thread;
    if (pthread_create(&thread, NULL, audio_dump_loop, NULL) != 0) return -1;
    pthread_detach(thread);
    return 0;
}