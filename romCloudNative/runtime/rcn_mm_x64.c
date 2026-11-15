// RomCloud Native : multimédia et réseau des jeux 64 bits (et de leurs DLL, FMOD par exemple).
//
// winmm : sortie de son waveOut vers celle de l'hôte (RcnHost.audio_open). Le jeu remet des tampons
// (WAVEHDR) ; le thread de son de l'hôte les lit au fil de l'eau, convertis en stéréo 16 bits, et les
// marque WHDR_DONE une fois joués (événement signalé si le jeu en a donné un). Pas d'entrée de son.
// ws2_32 : pas de réseau (WSAStartup réussit, toute socket échoue). msacm32 : aucun codec.
#include <stdlib.h>

#include "rcn_internal.h"

enum {
    MMSYSERR_NOERROR = 0,
    MMSYSERR_BADDEVICEID = 2,
    MMSYSERR_NODRIVER = 6,
    WAVERR_BADFORMAT = 32,
    WAVERR_STILLPLAYING = 33,
    WAVE_FORMAT_QUERY = 1,
    CALLBACK_TYPEMASK = 0x70000,
    CALLBACK_EVENT = 0x50000,
    WHDR_DONE = 1,
    WHDR_PREPARED = 2,
    WHDR_INQUEUE = 0x10,
    TIME_SAMPLES = 2,
    TIME_BYTES = 4,
    WAVE_HANDLE = 0x5701,
    QUEUE = 256,
};

static struct {
    pthread_mutex_t lock;
    RcnProcess *proc;
    int open;
    u32 channels, rate, bits, is_float, block;
    u64 queue[QUEUE]; // WAVEHDR en attente, dans l'ordre
    u32 head, count;
    u32 offset;       // octets déjà lus du premier tampon
    u64 played;       // octets joués depuis l'ouverture
    u32 callback_type;
    u64 callback;
    int host_state;   // 0 : pas encore ouverte, 1 : sortie de l'hôte, 2 : horloge seule (pas de son)
} wo = {.lock = PTHREAD_MUTEX_INITIALIZER};

static i32 read_sample(u64 at) {
    if (wo.is_float) {
        float f;
        memcpy(&f, PTR(at), 4);
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        return (i32)(f * 32767.0f);
    }
    switch (wo.bits) {
    case 8: return ((i32)rd8(at) - 128) << 8;
    case 16: return (i16)rd16(at);
    case 24: return (i32)((u32)rd8(at + 1) | (u32)rd8(at + 2) << 8) << 16 >> 16;
    default: return (i32)rd32(at) >> 16;
    }
}

// Thread de son de l'hôte : `frames` rames stéréo prises dans les tampons du jeu (silence s'il n'y en a pas).
static void fill(void *context, int16_t *out, int frames) {
    (void)context;
    pthread_mutex_lock(&wo.lock);
    u32 bytes_per_sample = wo.is_float ? 4 : wo.bits / 8;
    for (int f = 0; f < frames; f++) {
        if (!wo.open || !wo.count) {
            out[2 * f] = out[2 * f + 1] = 0;
            continue;
        }
        u64 hdr = wo.queue[wo.head];
        u64 data = rd64(hdr);
        u32 length = rd32(hdr + 8);
        if (wo.offset + wo.block <= length) {
            u64 frame = data + wo.offset;
            out[2 * f] = (int16_t)read_sample(frame);
            out[2 * f + 1] = (int16_t)read_sample(frame + (wo.channels > 1 ? bytes_per_sample : 0));
            wo.offset += wo.block;
            wo.played += wo.block;
        } else {
            out[2 * f] = out[2 * f + 1] = 0;
            wo.offset = length;
        }
        if (wo.offset >= length) {
            // Tampon joué : rendu au jeu.
            wr32(hdr + 24, (rd32(hdr + 24) & ~(u32)WHDR_INQUEUE) | WHDR_DONE);
            wo.head = (wo.head + 1) % QUEUE;
            wo.count--;
            wo.offset = 0;
            if (wo.callback_type == CALLBACK_EVENT && wo.callback) rcn_event_signal(wo.proc, wo.callback);
        }
    }
    pthread_mutex_unlock(&wo.lock);
}

// Sans sortie de son sur l'hôte : les tampons sont consommés au rythme du temps réel.
static void *clock_thread(void *arg) {
    (void)arg;
    rcn_thread_default_affinity();
    int16_t scratch[2 * 4800];
    for (;;) {
        rcn_sleep_ms(10);
        u32 frames = wo.rate / 100;
        if (frames > 4800) frames = 4800;
        fill(NULL, scratch, (int)frames);
    }
    return NULL;
}

static void start_output(RcnCtx *ctx) {
    if (wo.host_state) return;
    const RcnHost *host = ctx->proc->host;
    if (host->audio_open && host->audio_open(host->user, (int)wo.rate, fill, NULL) == 0) {
        wo.host_state = 1;
        return;
    }
    wo.host_state = 2;
    pthread_t t;
    pthread_create(&t, NULL, clock_thread, NULL);
    pthread_detach(t);
}

static void mm_timeGetTime(RcnCtx *ctx) { RET((u32)((rcn_now_ns() - ctx->proc->start_ns) / 1000000)); }
static void mm_One(RcnCtx *ctx) { RET(1); }
static void mm_Zero(RcnCtx *ctx) { RET(0); }
static void mm_NoDriver(RcnCtx *ctx) { RET(MMSYSERR_NODRIVER); }

// waveOutGetDevCapsW(device, &caps, size) : WAVEOUTCAPSW.
static void mm_waveOutGetDevCapsW(RcnCtx *ctx) {
    if ((u32)A0 != 0 && (u32)A0 != 0xFFFFFFFFu) {
        RET(MMSYSERR_BADDEVICEID);
        return;
    }
    u64 caps = A1;
    memset(PTR(caps), 0, 84 < (u32)A2 ? 84 : (u32)A2);
    static const char name[] = "RomCloud";
    for (int i = 0; name[i]; i++) wr16(caps + 8 + 2 * (u64)i, (u16)name[i]);
    wr32(caps + 72, 0xFFFFF); // tous les formats PCM usuels
    wr16(caps + 76, 2);
    RET(MMSYSERR_NOERROR);
}
// waveOutOpen(&handle, device, &format, callback, instance, flags)
static void mm_waveOutOpen(RcnCtx *ctx) {
    u64 format = A2;
    u32 flags = (u32)STK(5);
    u16 tag = rd16(format), channels = rd16(format + 2), bits = rd16(format + 14);
    u32 rate = rd32(format + 4), block = rd16(format + 12);
    int is_float = tag == 3;
    if (tag == 0xFFFE && rd16(format + 16) >= 22) is_float = rd32(format + 24) == 3;
    int ok = channels >= 1 && rate >= 8000 && rate <= 192000 && block > 0 &&
             (is_float ? bits == 32 : (bits == 8 || bits == 16 || bits == 24 || bits == 32)) &&
             (tag == 1 || tag == 3 || tag == 0xFFFE);
    if (!ok) {
        RET(WAVERR_BADFORMAT);
        return;
    }
    if (flags & WAVE_FORMAT_QUERY) {
        RET(MMSYSERR_NOERROR);
        return;
    }
    pthread_mutex_lock(&wo.lock);
    wo.proc = ctx->proc;
    wo.channels = channels;
    wo.rate = rate;
    wo.bits = bits;
    wo.is_float = is_float;
    wo.block = block;
    wo.head = wo.count = wo.offset = 0;
    wo.played = 0;
    wo.callback_type = flags & CALLBACK_TYPEMASK;
    wo.callback = A3;
    wo.open = 1;
    pthread_mutex_unlock(&wo.lock);
    if (A0) wr64(A0, WAVE_HANDLE);
    start_output(ctx);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutPrepareHeader(RcnCtx *ctx) {
    wr32(A1 + 24, (rd32(A1 + 24) | WHDR_PREPARED) & ~(u32)WHDR_DONE);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutUnprepareHeader(RcnCtx *ctx) {
    u32 flags = rd32(A1 + 24);
    if (flags & WHDR_INQUEUE) {
        RET(WAVERR_STILLPLAYING);
        return;
    }
    wr32(A1 + 24, flags & ~(u32)WHDR_PREPARED);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutWrite(RcnCtx *ctx) {
    pthread_mutex_lock(&wo.lock);
    if (wo.count < QUEUE) {
        wr32(A1 + 24, (rd32(A1 + 24) & ~(u32)WHDR_DONE) | WHDR_INQUEUE);
        wo.queue[(wo.head + wo.count++) % QUEUE] = A1;
    }
    pthread_mutex_unlock(&wo.lock);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutReset(RcnCtx *ctx) {
    pthread_mutex_lock(&wo.lock);
    while (wo.count) {
        u64 hdr = wo.queue[wo.head];
        wr32(hdr + 24, (rd32(hdr + 24) & ~(u32)WHDR_INQUEUE) | WHDR_DONE);
        wo.head = (wo.head + 1) % QUEUE;
        wo.count--;
    }
    wo.offset = 0;
    pthread_mutex_unlock(&wo.lock);
    if (wo.callback_type == CALLBACK_EVENT && wo.callback) rcn_event_signal(ctx->proc, wo.callback);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutClose(RcnCtx *ctx) {
    mm_waveOutReset(ctx);
    wo.open = 0;
    RET(MMSYSERR_NOERROR);
}
// waveOutGetPosition(handle, &mmtime, size)
static void mm_waveOutGetPosition(RcnCtx *ctx) {
    u64 t = A1;
    pthread_mutex_lock(&wo.lock);
    u64 played = wo.played;
    pthread_mutex_unlock(&wo.lock);
    if (rd32(t) == TIME_SAMPLES && wo.block) {
        wr32(t + 4, (u32)(played / wo.block));
    } else {
        wr32(t, TIME_BYTES);
        wr32(t + 4, (u32)played);
    }
    RET(MMSYSERR_NOERROR);
}

const RcnShim rcn_winmm_x64[] = {
    {"timeBeginPeriod", mm_Zero, 0},
    {"timeEndPeriod", mm_Zero, 0},
    {"timeGetTime", mm_timeGetTime, 0},
    {"waveInAddBuffer", mm_NoDriver, 0},
    {"waveInClose", mm_NoDriver, 0},
    {"waveInGetDevCapsW", mm_NoDriver, 0},
    {"waveInGetNumDevs", mm_Zero, 0},
    {"waveInOpen", mm_NoDriver, 0},
    {"waveInPrepareHeader", mm_NoDriver, 0},
    {"waveInReset", mm_NoDriver, 0},
    {"waveInStart", mm_NoDriver, 0},
    {"waveInUnprepareHeader", mm_NoDriver, 0},
    {"waveOutClose", mm_waveOutClose, 0},
    {"waveOutGetDevCapsW", mm_waveOutGetDevCapsW, 0},
    {"waveOutGetNumDevs", mm_One, 0},
    {"waveOutGetPosition", mm_waveOutGetPosition, 0},
    {"waveOutOpen", mm_waveOutOpen, 0},
    {"waveOutPrepareHeader", mm_waveOutPrepareHeader, 0},
    {"waveOutReset", mm_waveOutReset, 0},
    {"waveOutUnprepareHeader", mm_waveOutUnprepareHeader, 0},
    {"waveOutWrite", mm_waveOutWrite, 0},
};
const size_t rcn_winmm_x64_count = sizeof rcn_winmm_x64 / sizeof rcn_winmm_x64[0];

// --- ws2_32 (par numéro d'ordre surtout) ---

static void ws_Fail(RcnCtx *ctx) { RET_I32(-1); }       // SOCKET_ERROR
static void ws_Invalid(RcnCtx *ctx) { RET(~0ull); }     // INVALID_SOCKET
static void ws_LastError(RcnCtx *ctx) { RET(10050); }   // WSAENETDOWN
static void ws_Startup(RcnCtx *ctx) {
    if (A1) {
        memset(PTR(A1), 0, 408);
        wr16(A1, (u16)A0);
        wr16(A1 + 2, 0x0202);
    }
    RET(0);
}
static u32 swap32(u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }
static void ws_htonl(RcnCtx *ctx) { RET(swap32((u32)A0)); }
static void ws_htons(RcnCtx *ctx) { RET((u16)(((u16)A0 >> 8) | ((u16)A0 << 8))); }
static void ws_getaddrinfo(RcnCtx *ctx) {
    if (STK(3)) wr64(STK(3), 0);
    RET(11001); // WSAHOST_NOT_FOUND
}
static void ws_Nothing(RcnCtx *ctx) { (void)ctx; }
const RcnShim rcn_ws2_x64[] = {
    {"#1", ws_Invalid, 0},  {"#2", ws_Fail, 0},     {"#3", mm_Zero, 0},    {"#4", ws_Fail, 0},
    {"#5", ws_Fail, 0},     {"#6", ws_Fail, 0},     {"#7", ws_Fail, 0},    {"#8", ws_htonl, 0},
    {"#9", ws_htons, 0},    {"#10", ws_Fail, 0},    {"#13", ws_Fail, 0},   {"#14", ws_htonl, 0},
    {"#15", ws_htons, 0},   {"#16", ws_Fail, 0},    {"#17", ws_Fail, 0},   {"#18", ws_Fail, 0},
    {"#19", ws_Fail, 0},    {"#20", ws_Fail, 0},    {"#21", ws_Fail, 0},   {"#22", ws_Fail, 0},
    {"#23", ws_Invalid, 0}, {"#111", ws_LastError, 0}, {"#112", ws_Nothing, 0}, {"#115", ws_Startup, 0},
    {"#116", mm_Zero, 0},   {"WSAIoctl", ws_Fail, 0}, {"freeaddrinfo", ws_Nothing, 0}, {"getaddrinfo", ws_getaddrinfo, 0},
    {"WSAStartup", ws_Startup, 0}, {"WSACleanup", mm_Zero, 0}, {"WSAGetLastError", ws_LastError, 0},
    {"socket", ws_Invalid, 0}, {"closesocket", mm_Zero, 0},
};
const size_t rcn_ws2_x64_count = sizeof rcn_ws2_x64 / sizeof rcn_ws2_x64[0];

// --- msacm32 : aucun codec (ACMERR_NOTPOSSIBLE) ---

static void acm_NotPossible(RcnCtx *ctx) { RET(512); }
const RcnShim rcn_msacm_x64[] = {
    {"acmFormatSuggest", acm_NotPossible, 0},       {"acmStreamClose", acm_NotPossible, 0},
    {"acmStreamConvert", acm_NotPossible, 0},       {"acmStreamOpen", acm_NotPossible, 0},
    {"acmStreamPrepareHeader", acm_NotPossible, 0}, {"acmStreamSize", acm_NotPossible, 0},
    {"acmStreamUnprepareHeader", acm_NotPossible, 0},
};
const size_t rcn_msacm_x64_count = sizeof rcn_msacm_x64 / sizeof rcn_msacm_x64[0];
