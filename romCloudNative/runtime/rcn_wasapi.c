// RomCloud Native : WASAPI (MMDevAPI) pour les jeux 64 bits et leurs bibliothèques de son (FMOD…).
//
// Un seul périphérique de sortie, « RomCloud », au format de mélange flottant 32 bits stéréo à 48 kHz.
// Le jeu écrit dans le tampon de IAudioRenderClient (un par IAudioClient) ; le thread de son de l'hôte
// (RcnHost.audio_open) les mélange en stéréo 16 bits et signale l'événement de chaque flux à chaque passage.
// Pas d'entrée.
//
// Objets COM 64 bits : [0] table virtuelle, [8] compteur de références, puis leurs données (COM64_DATA).
#include <stdlib.h>

#include "rcn_internal.h"

enum {
    S_OK = 0,
    E_NOINTERFACE = 0x80004002u,
    E_POINTER = 0x80004003u,
    E_NOTFOUND = 0x80070490u,
    AUDCLNT_E_NOT_INITIALIZED = 0x88890001u,
    AUDCLNT_E_BUFFER_TOO_LARGE = 0x88890006u,
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK = 0x40000,
    AUDCLNT_BUFFERFLAGS_SILENT = 2,
    MIX_RATE = 48000,
    MIX_CHANNELS = 2,
    VT_LPWSTR = 31,
    VT_BLOB = 65,
};

// --- Objets COM 64 bits ---

typedef struct {
    const char *name;
    RcnFn fn;
} Method;

// Table virtuelle d'une interface, construite une fois (relais de l'hôte).
static u64 vtable(RcnCtx *ctx, const char *iface, const Method *methods, u32 count, u64 *cache) {
    if (*cache) return *cache;
    u64 *table = rcn_galloc(ctx, count * 8, 1);
    for (u32 i = 0; table && i < count; i++) table[i] = rcn_thunk(ctx, iface, methods[i].name, methods[i].fn, 0);
    *cache = (u64)(uintptr_t)table;
    return *cache;
}
static u64 com_new(RcnCtx *ctx, u64 table, size_t data_size) {
    u8 *object = rcn_galloc(ctx, 16 + data_size, 1);
    if (!object) return 0;
    memcpy(object, &table, 8);
    *(u32 *)(object + 8) = 1;
    return (u64)(uintptr_t)object;
}
#define DATA(type, object) ((type *)((u8 *)PTR(object) + 16))

static void com_AddRef(RcnCtx *ctx) { RET(++*(u32 *)((u8 *)PTR(A0) + 8)); }
// Les objets ne sont jamais libérés (peu nombreux, créés à l'initialisation du son).
static void com_Release(RcnCtx *ctx) {
    u32 *refs = (u32 *)((u8 *)PTR(A0) + 8);
    if (*refs) --*refs;
    RET(*refs);
}
static void com_QueryInterface(RcnCtx *ctx) {
    // Le jeu demande la même interface (ou IUnknown) : l'objet lui-même.
    if (!A2) {
        RET(E_POINTER);
        return;
    }
    wr64(A2, A0);
    com_AddRef(ctx);
    RET(S_OK);
}
static void com_Ok(RcnCtx *ctx) { RET(S_OK); }
static void com_NotImpl(RcnCtx *ctx) { RET(0x80004001u); }

static u64 alloc_wide(RcnCtx *ctx, const char *text) {
    size_t n = strlen(text);
    u16 *p = rcn_galloc(ctx, 2 * (n + 1), 0);
    if (!p) return 0;
    for (size_t i = 0; i <= n; i++) p[i] = (u16)(u8)text[i];
    return (u64)(uintptr_t)p;
}

// WAVEFORMATEXTENSIBLE du mélange : flottant 32 bits, stéréo, 48 kHz.
static void write_mix_format(u64 f) {
    memset(PTR(f), 0, 40);
    wr16(f, 0xFFFE);
    wr16(f + 2, MIX_CHANNELS);
    wr32(f + 4, MIX_RATE);
    wr32(f + 8, MIX_RATE * MIX_CHANNELS * 4);
    wr16(f + 12, MIX_CHANNELS * 4);
    wr16(f + 14, 32);
    wr16(f + 16, 22);
    wr16(f + 18, 32);
    wr32(f + 20, 3); // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
    // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT {00000003-0000-0010-8000-00aa00389b71}
    static const u8 sub[16] = {3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
    memcpy(PTR(f + 24), sub, 16);
}

// --- Flux de sortie (un par IAudioClient, mélangés) ---
// Un jeu peut ouvrir plusieurs clients : NieR: Automata ouvre celui de Wwise (piloté par événement) puis un
// second (vidéos), qui écrit du silence. Avec un état unique, le second écrasait le premier (son événement
// compris) : Wwise attendait pour toujours et le jeu restait muet. Chaque client a donc son tampon ; le
// thread de son de l'hôte (48 kHz) les mélange, au plus proche pour un flux à une autre fréquence.

enum { STREAMS = 8 };
typedef struct {
    int initialized, started;
    u32 channels, bits, is_float, block, rate;
    u32 frames;        // taille du tampon (rames)
    u8 *ring;          // frames * block octets
    u32 read, queued;  // position de lecture, rames en attente
    u32 step;          // pas de lecture par rame de sortie (16.16)
    u32 phase;         // fraction de rame déjà lue (16.16)
    u8 *staging;       // tampon rendu par GetBuffer
    u32 staging_frames;
    u64 event;
    u64 played;        // rames jouées
    u32 refs;          // objets du jeu qui s'en servent (client, rendu, horloge)
} Stream;

static struct {
    pthread_mutex_t lock;
    RcnProcess *proc;
    int host_state;
    Stream *streams[STREAMS];
    u64 starved, delivered, report_ns; // rames de sortie sans donnée / avec (suivi)
} st = {.lock = PTHREAD_MUTEX_INITIALIZER};

// Flux de l'objet (IAudioClient, IAudioRenderClient, IAudioClock) : pointeur de l'hôte dans ses données.
#define STREAM(object) (*DATA(Stream *, object))

static i32 sample_at(const Stream *s, const u8 *p) {
    if (s->is_float) {
        float f;
        memcpy(&f, p, 4);
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        return (i32)(f * 32767.0f);
    }
    if (s->bits == 16) {
        i16 v;
        memcpy(&v, p, 2);
        return v;
    }
    if (s->bits == 24) return (i32)((u32)p[1] | (u32)p[2] << 8) << 16 >> 16;
    i32 v;
    memcpy(&v, p, 4);
    return v >> 16;
}

static void fill(void *context, int16_t *out, int count) {
    (void)context;
    static int log_audio = -1;
    if (log_audio < 0) log_audio = getenv("RCN_AUDIO_LOG") != NULL;
    int peak = 0;
    u64 events[STREAMS];
    int n_events = 0;
    pthread_mutex_lock(&st.lock);
    for (int i = 0; i < count; i++) {
        i32 left = 0, right = 0;
        int fed = 0, wanted = 0;
        for (int k = 0; k < STREAMS; k++) {
            Stream *s = st.streams[k];
            if (!s || !s->started) continue;
            wanted = 1;
            if (!s->queued) continue;
            fed = 1;
            u32 bps = s->is_float ? 4 : s->bits / 8;
            const u8 *frame = s->ring + (size_t)s->read * s->block;
            left += sample_at(s, frame);
            right += sample_at(s, frame + (s->channels > 1 ? bps : 0));
            // Rames du flux consommées pour cette rame de sortie (fréquence du flux / 48 kHz).
            s->phase += s->step;
            u32 advance = s->phase >> 16;
            s->phase &= 0xFFFF;
            for (u32 a = 0; a < advance && s->queued; a++) {
                s->read = (s->read + 1) % s->frames;
                s->queued--;
                s->played++;
            }
        }
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        out[2 * i] = (int16_t)left;
        out[2 * i + 1] = (int16_t)right;
        if (abs(left) > peak) peak = abs(left);
        if (abs(right) > peak) peak = abs(right);
        if (fed) st.delivered++;
        else if (wanted) st.starved++;
    }
    for (int k = 0; k < STREAMS; k++) {
        Stream *s = st.streams[k];
        if (s && s->started && s->event) events[n_events++] = s->event;
    }
    u64 now = rcn_now_ns();
    if (now - st.report_ns >= 5000000000ull) {
        if (st.report_ns) {
            u32 queued = 0;
            for (int k = 0; k < STREAMS; k++)
                if (st.streams[k] && st.streams[k]->started) queued += st.streams[k]->queued;
            rcn_log("rcn : son %llu rames jouees, %llu manquantes, %u en attente", (unsigned long long)st.delivered, (unsigned long long)st.starved, queued);
        }
        if (log_audio) rcn_log("rcn : son, crete du dernier tampon %d", peak);
        st.report_ns = now;
        st.starved = st.delivered = 0;
    }
    RcnProcess *proc = st.proc;
    pthread_mutex_unlock(&st.lock);
    for (int k = 0; k < n_events; k++) rcn_event_signal(proc, events[k]);
}

static void *clock_thread(void *arg) {
    (void)arg;
    rcn_thread_default_affinity();
    int16_t scratch[2 * 480];
    for (;;) {
        rcn_sleep_ms(10);
        fill(NULL, scratch, 480);
    }
    return NULL;
}

static void start_host(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    int state = st.host_state;
    if (!state) st.host_state = 3; // ouverture en cours
    pthread_mutex_unlock(&st.lock);
    if (state) return;
    const RcnHost *host = ctx->proc->host;
    if (host->audio_open && host->audio_open(host->user, MIX_RATE, fill, NULL) == 0) {
        st.host_state = 1;
        rcn_log("rcn : son WASAPI -> hote, %u Hz, flux melanges", MIX_RATE);
        return;
    }
    st.host_state = 2; // pas de son sur l'hôte : le temps passe quand même
    pthread_t t;
    pthread_create(&t, NULL, clock_thread, NULL);
    pthread_detach(t);
}

// --- IAudioRenderClient ---

// GetBuffer(this, frames, &data)
static void rc_GetBuffer(RcnCtx *ctx) {
    Stream *s = STREAM(A0);
    u32 n = (u32)A1;
    pthread_mutex_lock(&st.lock);
    u32 room = s->frames - s->queued;
    pthread_mutex_unlock(&st.lock);
    if (n > room) {
        RET(AUDCLNT_E_BUFFER_TOO_LARGE);
        return;
    }
    if (n > s->staging_frames) {
        free(s->staging);
        s->staging = calloc(n ? n : 1, s->block);
        s->staging_frames = s->staging ? n : 0;
    }
    wr64(A2, (u64)(uintptr_t)s->staging);
    RET(s->staging ? S_OK : 0x8007000Eu);
}
// ReleaseBuffer(this, frames, flags)
static void rc_ReleaseBuffer(RcnCtx *ctx) {
    Stream *s = STREAM(A0);
    u32 n = (u32)A1, silent = ((u32)A2 & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    static int log_audio = -1;
    if (log_audio < 0) log_audio = getenv("RCN_AUDIO_LOG") != NULL;
    if (log_audio) {
        // Bilan toutes les 3 s : appels, rames, crête des tampons rendus (flottants).
        static u32 last, calls, frames, silent_calls;
        static int peak;
        calls++;
        frames += n;
        silent_calls += silent;
        if (!silent && s->is_float)
            for (u32 i = 0; i < n * s->block && s->staging; i += 4) {
                float f;
                memcpy(&f, s->staging + i, 4);
                int v = (int)(f * 32767.0f);
                if (v < 0) v = -v;
                if (v > peak) peak = v;
            }
        u32 now = (u32)(rcn_now_ns() / 1000000);
        if (now - last >= 3000) {
            last = now;
            rcn_log("rcn : ReleaseBuffer %u appels (%u silencieux), %u rames, crete %d", calls, silent_calls, frames, peak);
            calls = frames = silent_calls = 0;
            peak = 0;
        }
    }
    pthread_mutex_lock(&st.lock);
    for (u32 i = 0; i < n && s->queued < s->frames; i++) {
        u32 at = (s->read + s->queued) % s->frames;
        if (silent || !s->staging) memset(s->ring + (size_t)at * s->block, 0, s->block);
        else memcpy(s->ring + (size_t)at * s->block, s->staging + (size_t)i * s->block, s->block);
        s->queued++;
    }
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static void stream_Release(RcnCtx *ctx);
static const Method render_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", stream_Release},
    {"GetBuffer", rc_GetBuffer},           {"ReleaseBuffer", rc_ReleaseBuffer},
};

// --- IAudioClock ---

static void clk_GetFrequency(RcnCtx *ctx) {
    wr64(A1, STREAM(A0)->rate);
    RET(S_OK);
}
static void clk_GetPosition(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    u64 p = STREAM(A0)->played;
    pthread_mutex_unlock(&st.lock);
    wr64(A1, p);
    if (A2) wr64(A2, rcn_now_ns() / 100);
    RET(S_OK);
}
static void clk_GetCharacteristics(RcnCtx *ctx) {
    wr32(A1, 0);
    RET(S_OK);
}
static const Method clock_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", stream_Release},
    {"GetFrequency", clk_GetFrequency},     {"GetPosition", clk_GetPosition}, {"GetCharacteristics", clk_GetCharacteristics},
};

// --- IAudioClient ---

static u64 render_cache, clock_cache;
// Initialize(this, mode, flags, duration, period, &format, &session)
static void ac_Initialize(RcnCtx *ctx) {
    Stream *s = STREAM(A0);
    u32 flags = (u32)A2;
    i64 duration = (i64)A3;
    u64 f = STK(5);
    u16 tag = rd16(f), channels = rd16(f + 2), bits = rd16(f + 14), block = rd16(f + 12);
    u32 rate = rd32(f + 4);
    int is_float = tag == 3 || (tag == 0xFFFE && rd32(f + 24) == 3);
    static int log_audio = -1;
    if (log_audio < 0) log_audio = getenv("RCN_AUDIO_LOG") != NULL;
    if (log_audio)
        rcn_log("rcn : IAudioClient %llx Initialize mode %u, options %x, duree %lld, format %x %u voies %u bits %u Hz", (unsigned long long)A0,
                (u32)A1, flags, (long long)duration, tag, channels, bits, rate);
    if (!channels || !block || !rate) {
        RET(0x88890008u); // AUDCLNT_E_UNSUPPORTED_FORMAT
        return;
    }
    // Tampon : la durée demandée, 40 ms au moins.
    u32 frames = (u32)((duration > 0 ? duration : 400000) * (i64)rate / 10000000);
    if (frames < rate / 25) frames = rate / 25;
    u8 *ring = calloc(frames, block);
    pthread_mutex_lock(&st.lock);
    st.proc = ctx->proc;
    s->channels = channels;
    s->bits = bits;
    s->is_float = is_float;
    s->block = block;
    s->rate = rate;
    s->step = (u32)(((u64)rate << 16) / MIX_RATE);
    s->phase = 0;
    free(s->ring);
    s->frames = frames;
    s->ring = ring;
    s->read = s->queued = 0;
    s->played = 0;
    s->initialized = ring != NULL;
    s->event = 0;
    pthread_mutex_unlock(&st.lock);
    RET(s->initialized ? S_OK : 0x8007000Eu);
}
static void ac_GetBufferSize(RcnCtx *ctx) {
    Stream *s = STREAM(A0);
    if (!s->initialized) {
        RET(AUDCLNT_E_NOT_INITIALIZED);
        return;
    }
    wr32(A1, s->frames);
    RET(S_OK);
}
static void ac_GetStreamLatency(RcnCtx *ctx) {
    wr64(A1, 100000);
    RET(S_OK);
}
static void ac_GetCurrentPadding(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    wr32(A1, STREAM(A0)->queued);
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
// IsFormatSupported(this, mode, &format, &closest) : tout format PCM ou flottant que la conversion sait lire.
static void ac_IsFormatSupported(RcnCtx *ctx) {
    if (STK(3)) wr64(STK(3), 0);
    u64 f = A2;
    u16 tag = rd16(f), bits = rd16(f + 14);
    int ok = (tag == 1 || tag == 3 || tag == 0xFFFE) && (bits == 16 || bits == 24 || bits == 32) && rd16(f + 2) >= 1;
    RET(ok ? S_OK : 0x88890008u);
}
static void ac_GetMixFormat(RcnCtx *ctx) {
    u64 f = (u64)(uintptr_t)rcn_galloc(ctx, 40, 1);
    if (f) write_mix_format(f);
    wr64(A1, f);
    RET(f ? S_OK : 0x8007000Eu);
}
static void ac_GetDevicePeriod(RcnCtx *ctx) {
    if (A1) wr64(A1, 100000); // 10 ms
    if (A2) wr64(A2, 30000);
    RET(S_OK);
}
static void ac_Start(RcnCtx *ctx) {
    Stream *s = STREAM(A0);
    start_host(ctx);
    pthread_mutex_lock(&st.lock);
    s->started = 1;
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static void ac_Stop(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    STREAM(A0)->started = 0;
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static void ac_Reset(RcnCtx *ctx) {
    Stream *s = STREAM(A0);
    pthread_mutex_lock(&st.lock);
    s->read = s->queued = 0;
    s->phase = 0;
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static void ac_SetEventHandle(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    STREAM(A0)->event = A1;
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
// Release du client, du rendu et de l'horloge : quand plus aucun objet ne se sert du flux, il quitte le
// mélange (sa place est reprise ; la mémoire reste, un objet périmé du jeu ne lit rien de libéré).
static void stream_Release(RcnCtx *ctx) {
    u32 *refs = (u32 *)((u8 *)PTR(A0) + 8);
    if (*refs) --*refs;
    if (!*refs) {
        pthread_mutex_lock(&st.lock);
        Stream *s = STREAM(A0);
        if (s->refs && !--s->refs) {
            s->started = 0;
            for (int k = 0; k < STREAMS; k++)
                if (st.streams[k] == s) st.streams[k] = NULL;
        }
        pthread_mutex_unlock(&st.lock);
    }
    RET(*refs);
}
// Objet (rendu, horloge) qui se sert du flux.
static u64 stream_object(RcnCtx *ctx, u64 table, Stream *s) {
    u64 object = com_new(ctx, table, 8);
    if (!object) return 0;
    STREAM(object) = s;
    pthread_mutex_lock(&st.lock);
    s->refs++;
    pthread_mutex_unlock(&st.lock);
    return object;
}
// Nouveau flux, inscrit au mélange ; NULL s'il y en a déjà trop.
static Stream *stream_new(void) {
    Stream *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->refs = 1;
    pthread_mutex_lock(&st.lock);
    int k = 0;
    while (k < STREAMS && st.streams[k]) k++;
    if (k < STREAMS) st.streams[k] = s;
    pthread_mutex_unlock(&st.lock);
    if (k == STREAMS) {
        free(s);
        rcn_log("rcn : WASAPI, plus de %d flux ouverts a la fois : refuse", STREAMS);
        return NULL;
    }
    return s;
}
// Tables du volume et de la session (définies plus bas, avec IMMDevice).
static const Method simple_volume_methods[], session_methods[];
static u64 simple_cache, session_cache;
// GetService(this, &iid, &object)
static void ac_GetService(RcnCtx *ctx) {
    u32 id = A1 ? rd32(A1) : 0;
    u64 object = 0;
    if (id == 0xF294ACFCu) {
        object = stream_object(ctx, vtable(ctx, "IAudioRenderClient", render_methods, 5, &render_cache), STREAM(A0));
    } else if (id == 0xCD63314Fu) {
        object = stream_object(ctx, vtable(ctx, "IAudioClock", clock_methods, 6, &clock_cache), STREAM(A0));
    } else if (id == 0x87CE5498u) { // ISimpleAudioVolume
        object = com_new(ctx, vtable(ctx, "ISimpleAudioVolume", simple_volume_methods, 7, &simple_cache), 0);
    } else if (id == 0xE2F5BB11u || id == 0xBF448BEFu) { // IAudioSessionControl(2)
        object = com_new(ctx, vtable(ctx, "IAudioSessionControl", session_methods, 12, &session_cache), 0);
    }
    if (A2) wr64(A2, object);
    RET(object ? S_OK : E_NOINTERFACE);
}
// IsOffloadCapable(catégorie, &oui)
static void ac_IsOffloadCapable(RcnCtx *ctx) {
    if (A2) wr32(A2, 0);
    RET(S_OK);
}
// GetBufferSizeLimits(format, événements, &min, &max) : en centaines de nanosecondes.
static void ac_GetBufferSizeLimits(RcnCtx *ctx) {
    if (A3) wr64(A3, 100000);    // 10 ms
    if (STK(4)) wr64(STK(4), 20000000); // 2 s
    RET(S_OK);
}
// GetSharedModeEnginePeriod(format, &défaut, &pas, &min, &max) : 480 images (10 ms à 48 kHz).
static void ac_GetSharedModeEnginePeriod(RcnCtx *ctx) {
    if (A2) wr32(A2, 480);
    if (A3) wr32(A3, 480);
    if (STK(4)) wr32(STK(4), 480);
    if (STK(5)) wr32(STK(5), 480);
    RET(S_OK);
}
// GetCurrentSharedModeEnginePeriod(&format, &période)
static void ac_GetCurrentSharedModeEnginePeriod(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    if (A2) wr32(A2, 480);
    RET(A1 ? 0x80004001u : S_OK);
}
static const Method client_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef},
    {"Release", stream_Release},            {"Initialize", ac_Initialize},
    {"GetBufferSize", ac_GetBufferSize},     {"GetStreamLatency", ac_GetStreamLatency},
    {"GetCurrentPadding", ac_GetCurrentPadding}, {"IsFormatSupported", ac_IsFormatSupported},
    {"GetMixFormat", ac_GetMixFormat},       {"GetDevicePeriod", ac_GetDevicePeriod},
    {"Start", ac_Start},                     {"Stop", ac_Stop},
    {"Reset", ac_Reset},                     {"SetEventHandle", ac_SetEventHandle},
    {"GetService", ac_GetService},
    // IAudioClient2
    {"IsOffloadCapable", ac_IsOffloadCapable}, {"SetClientProperties", com_Ok},
    {"GetBufferSizeLimits", ac_GetBufferSizeLimits},
    // IAudioClient3 : périodes du mode partagé ; flux à période choisie non proposé (Initialize suffit).
    {"GetSharedModeEnginePeriod", ac_GetSharedModeEnginePeriod},
    {"GetCurrentSharedModeEnginePeriod", ac_GetCurrentSharedModeEnginePeriod},
    {"InitializeSharedAudioStream", com_NotImpl},
};

// --- IPropertyStore : nom et format du périphérique ---

static void ps_GetCount(RcnCtx *ctx) {
    wr32(A1, 2);
    RET(S_OK);
}
// GetValue(this, &key, &propvariant)
static void ps_GetValue(RcnCtx *ctx) {
    u64 key = A1, v = A2;
    memset(PTR(v), 0, 24);
    u32 id = rd32(key), pid = rd32(key + 16);
    if (id == 0x1DA5D803u && pid == 4) { // PKEY_AudioEndpoint_GUID : celui de l'énumération DirectSound
        wr16(v, VT_LPWSTR);
        wr64(v + 8, alloc_wide(ctx, "{52434E00-0000-4000-8000-524F4D434C44}"));
    } else if (id == 0xA45C254Eu && pid == 14) { // PKEY_Device_FriendlyName
        wr16(v, VT_LPWSTR);
        wr64(v + 8, alloc_wide(ctx, "RomCloud"));
    } else if (id == 0xB3F8FA53u && pid == 2) { // PKEY_Device_DeviceDesc
        wr16(v, VT_LPWSTR);
        wr64(v + 8, alloc_wide(ctx, "RomCloud"));
    } else if (id == 0xF19F064Du) { // PKEY_AudioEngine_DeviceFormat
        u64 f = (u64)(uintptr_t)rcn_galloc(ctx, 40, 1);
        if (f) write_mix_format(f);
        wr16(v, VT_BLOB);
        wr32(v + 8, 40);
        wr64(v + 16, f);
    }
    RET(S_OK);
}
static const Method store_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetCount", ps_GetCount},              {"GetAt", com_NotImpl}, {"GetValue", ps_GetValue},
    {"SetValue", com_NotImpl},              {"Commit", com_NotImpl},
};

// --- Volume du système (IAudioEndpointVolume, ISimpleAudioVolume, IAudioSessionControl) ---
// Le jeu lit le volume du périphérique et s'y accorde : sans ces interfaces, il prenait le silence comme
// volume maître et n'écrivait que des zéros dans le tampon de rendu (NieR: Automata : aucun son).

static void vol_Ok(RcnCtx *ctx) { (void)ctx; RET(S_OK); }
static void vol_GetMasterVolumeLevel(RcnCtx *ctx) { if (A1) wr32(A1, 0); RET(S_OK); }          // 0 dB
static void vol_GetMasterVolumeLevelScalar(RcnCtx *ctx) { if (A1) wr32(A1, 0x3F800000u); RET(S_OK); } // 1.0
static void vol_GetChannelCount(RcnCtx *ctx) { if (A1) wr32(A1, 2); RET(S_OK); }
static void vol_GetChannelVolumeLevel(RcnCtx *ctx) { if (A2) wr32(A2, 0); RET(S_OK); }
static void vol_GetChannelVolumeLevelScalar(RcnCtx *ctx) { if (A2) wr32(A2, 0x3F800000u); RET(S_OK); }
static void vol_GetMute(RcnCtx *ctx) { if (A1) wr32(A1, 0); RET(S_OK); }                        // non coupé
static void vol_GetVolumeStepInfo(RcnCtx *ctx) {
    if (A1) wr32(A1, 1);
    if (A2) wr32(A2, 1);
    RET(S_OK);
}
static void vol_QueryHardwareSupport(RcnCtx *ctx) { if (A1) wr32(A1, 7); RET(S_OK); } // volume et sourdine
static void vol_GetVolumeRange(RcnCtx *ctx) {
    if (A1) wr32(A1, 0xC2C00000u); // -96 dB
    if (A2) wr32(A2, 0);           // 0 dB
    if (A3) wr32(A3, 0x41400000u); // 12 dB
    RET(S_OK);
}
static void sess_GetState(RcnCtx *ctx) { if (A1) wr32(A1, 1); RET(S_OK); } // AudioSessionStateActive
static const Method endpoint_volume_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"RegisterControlChangeNotify", com_NotImpl}, {"UnregisterControlChangeNotify", com_NotImpl},
    {"GetChannelCount", vol_GetChannelCount}, {"SetMasterVolumeLevel", vol_Ok},
    {"SetMasterVolumeLevelScalar", vol_Ok}, {"GetMasterVolumeLevel", vol_GetMasterVolumeLevel},
    {"GetMasterVolumeLevelScalar", vol_GetMasterVolumeLevelScalar}, {"SetChannelVolumeLevel", vol_Ok},
    {"SetChannelVolumeLevelScalar", vol_Ok}, {"GetChannelVolumeLevel", vol_GetChannelVolumeLevel},
    {"GetChannelVolumeLevelScalar", vol_GetChannelVolumeLevelScalar}, {"SetMute", vol_Ok}, {"GetMute", vol_GetMute},
    {"GetVolumeStepInfo", vol_GetVolumeStepInfo}, {"VolumeStepUp", vol_Ok}, {"VolumeStepDown", vol_Ok},
    {"QueryHardwareSupport", vol_QueryHardwareSupport}, {"GetVolumeRange", vol_GetVolumeRange},
};
static const Method simple_volume_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"SetMasterVolume", vol_Ok}, {"GetMasterVolume", vol_GetMasterVolumeLevelScalar}, {"SetMute", vol_Ok},
    {"GetMute", vol_GetMute},
};
static const Method session_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetState", sess_GetState}, {"GetDisplayName", com_NotImpl}, {"SetDisplayName", vol_Ok},
    {"SetIconPath", vol_Ok}, {"GetIconPath", com_NotImpl}, {"SetGroupingParam", vol_Ok},
    {"GetGroupingParam", com_NotImpl}, {"RegisterAudioSessionNotification", com_NotImpl},
    {"UnregisterAudioSessionNotification", com_NotImpl},
};

// --- IMMDevice ---

static u64 client_cache, store_cache, device_cache, collection_cache, enumerator_cache, device_object;
static u64 volume_cache;
// Activate(this, &iid, context, params, &object)
static void dev_Activate(RcnCtx *ctx) {
    u32 id = A1 ? rd32(A1) : 0;
    u64 object = 0;
    if (id == 0x1CB9AD4Cu || id == 0x726778CDu || id == 0x7ED4EE07u) { // IAudioClient, IAudioClient2, IAudioClient3
        Stream *s = stream_new();
        object = s ? com_new(ctx, vtable(ctx, "IAudioClient", client_methods, 21, &client_cache), 8) : 0;
        if (object) STREAM(object) = s;
    } else if (id == 0x5CDF2C82u) { // IAudioEndpointVolume
        object = com_new(ctx, vtable(ctx, "IAudioEndpointVolume", endpoint_volume_methods, 21, &volume_cache), 0);
    } else if (id == 0x87CE5498u) { // ISimpleAudioVolume
        object = com_new(ctx, vtable(ctx, "ISimpleAudioVolume", simple_volume_methods, 7, &simple_cache), 0);
    } else if (id == 0xE2F5BB11u || id == 0xBF448BEFu) { // IAudioSessionControl, IAudioSessionControl2
        object = com_new(ctx, vtable(ctx, "IAudioSessionControl", session_methods, 12, &session_cache), 0);
    }
    if (STK(4)) wr64(STK(4), object);
    RET(object ? S_OK : E_NOINTERFACE);
}
static void dev_OpenPropertyStore(RcnCtx *ctx) {
    u64 object = com_new(ctx, vtable(ctx, "IPropertyStore", store_methods, 8, &store_cache), 0);
    wr64(A2, object);
    RET(object ? S_OK : 0x8007000Eu);
}
static void dev_GetId(RcnCtx *ctx) {
    wr64(A1, alloc_wide(ctx, "{0.0.0.00000000}.{romcloud-0000}"));
    RET(S_OK);
}
static void dev_GetState(RcnCtx *ctx) {
    wr32(A1, 1); // DEVICE_STATE_ACTIVE
    RET(S_OK);
}
// IMMEndpoint (QueryInterface du périphérique) : GetDataFlow → eRender.
static void endpoint_GetDataFlow(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(A1 ? S_OK : E_POINTER);
}
static const Method endpoint_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetDataFlow", endpoint_GetDataFlow},
};
static u64 endpoint_cache;
// QueryInterface du périphérique : IMMEndpoint {1BE09788-…} est un autre objet ; IUnknown et IMMDevice,
// lui-même ; le reste refusé.
static void dev_QueryInterface(RcnCtx *ctx) {
    if (!A2) {
        RET(E_POINTER);
        return;
    }
    u32 id = A1 ? rd32(A1) : 0;
    if (id == 0x1BE09788u) {
        u64 object = com_new(ctx, vtable(ctx, "IMMEndpoint", endpoint_methods, 4, &endpoint_cache), 0);
        wr64(A2, object);
        RET(object ? S_OK : 0x8007000Eu);
        return;
    }
    if (id == 0 || id == 0xD666063Fu) { // IUnknown, IMMDevice
        com_QueryInterface(ctx);
        return;
    }
    wr64(A2, 0);
    RET(E_NOINTERFACE);
}
static const Method device_methods[] = {
    {"QueryInterface", dev_QueryInterface}, {"AddRef", com_AddRef},          {"Release", com_Release},
    {"Activate", dev_Activate},             {"OpenPropertyStore", dev_OpenPropertyStore},
    {"GetId", dev_GetId},                   {"GetState", dev_GetState},
};
static u64 device(RcnCtx *ctx) {
    if (!device_object) device_object = com_new(ctx, vtable(ctx, "IMMDevice", device_methods, 7, &device_cache), 0);
    return device_object;
}

// --- IMMDeviceCollection (données : nombre de périphériques) ---

static void col_GetCount(RcnCtx *ctx) {
    wr32(A1, *DATA(u32, A0));
    RET(S_OK);
}
static void col_Item(RcnCtx *ctx) {
    if ((u32)A1 >= *DATA(u32, A0)) {
        RET(E_NOTFOUND);
        return;
    }
    wr64(A2, device(ctx));
    RET(S_OK);
}
static const Method collection_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetCount", col_GetCount},             {"Item", col_Item},
};

// --- IMMDeviceEnumerator ---

// EnumAudioEndpoints(this, flow, mask, &collection) : la sortie seulement (eRender 0, eAll 2).
static void en_EnumAudioEndpoints(RcnCtx *ctx) {
    u64 object = com_new(ctx, vtable(ctx, "IMMDeviceCollection", collection_methods, 5, &collection_cache), 4);
    if (object) *DATA(u32, object) = (u32)A1 == 0 || (u32)A1 == 2 ? 1 : 0;
    wr64(A3, object);
    RET(object ? S_OK : 0x8007000Eu);
}
// GetDefaultAudioEndpoint(this, flow, role, &device)
static void en_GetDefaultAudioEndpoint(RcnCtx *ctx) {
    if ((u32)A1 != 0) {
        wr64(A3, 0);
        RET(E_NOTFOUND);
        return;
    }
    wr64(A3, device(ctx));
    RET(S_OK);
}
static void en_GetDevice(RcnCtx *ctx) {
    wr64(A2, device(ctx));
    RET(S_OK);
}
static const Method enumerator_methods[] = {
    {"QueryInterface", com_QueryInterface},
    {"AddRef", com_AddRef},
    {"Release", com_Release},
    {"EnumAudioEndpoints", en_EnumAudioEndpoints},
    {"GetDefaultAudioEndpoint", en_GetDefaultAudioEndpoint},
    {"GetDevice", en_GetDevice},
    {"RegisterEndpointNotificationCallback", com_Ok},
    {"UnregisterEndpointNotificationCallback", com_Ok},
};

// Objet créé par CoCreateInstance pour ce CLSID (premier mot), ou 0.
u64 rcn_wasapi_create(RcnCtx *ctx, u32 clsid_data1) {
    if (clsid_data1 != 0xBCDE0395u) return 0; // CLSID_MMDeviceEnumerator
    return com_new(ctx, vtable(ctx, "IMMDeviceEnumerator", enumerator_methods, 8, &enumerator_cache), 0);
}
