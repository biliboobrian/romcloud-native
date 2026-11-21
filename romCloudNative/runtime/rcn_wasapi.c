// RomCloud Native : WASAPI (MMDevAPI) pour les jeux 64 bits et leurs bibliothèques de son (FMOD…).
//
// Un seul périphérique de sortie, « RomCloud », au format de mélange flottant 32 bits stéréo à 48 kHz.
// Le jeu écrit dans le tampon de IAudioRenderClient ; le thread de son de l'hôte (RcnHost.audio_open)
// le lit, converti en stéréo 16 bits, et signale l'événement du jeu à chaque passage. Pas d'entrée.
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

// --- Flux de sortie (un seul) ---

static struct {
    pthread_mutex_t lock;
    RcnProcess *proc;
    int initialized, started, host_state;
    u32 channels, bits, is_float, block, rate;
    u32 frames;        // taille du tampon (rames)
    u8 *ring;          // frames * block octets
    u32 read, queued;  // position de lecture, rames en attente
    u8 *staging;       // tampon rendu par GetBuffer
    u32 staging_frames;
    u64 event;
    u64 played;        // rames jouées
    u64 starved, delivered, report_ns; // rames manquantes / fournies (suivi)
} st = {.lock = PTHREAD_MUTEX_INITIALIZER};

static i32 sample_at(const u8 *p) {
    if (st.is_float) {
        float f;
        memcpy(&f, p, 4);
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        return (i32)(f * 32767.0f);
    }
    if (st.bits == 16) {
        i16 v;
        memcpy(&v, p, 2);
        return v;
    }
    if (st.bits == 24) return (i32)((u32)p[1] | (u32)p[2] << 8) << 16 >> 16;
    i32 v;
    memcpy(&v, p, 4);
    return v >> 16;
}

static void fill(void *context, int16_t *out, int count) {
    (void)context;
    static int log_audio = -1;
    if (log_audio < 0) log_audio = getenv("RCN_AUDIO_LOG") != NULL;
    int peak = 0;
    pthread_mutex_lock(&st.lock);
    u32 bps = st.is_float ? 4 : st.bits / 8;
    for (int i = 0; i < count; i++) {
        if (!st.started || !st.queued) {
            out[2 * i] = out[2 * i + 1] = 0;
            if (st.started) st.starved++;
            continue;
        }
        st.delivered++;
        const u8 *frame = st.ring + (size_t)st.read * st.block;
        out[2 * i] = (int16_t)sample_at(frame);
        out[2 * i + 1] = (int16_t)sample_at(frame + (st.channels > 1 ? bps : 0));
        if (abs(out[2 * i]) > peak) peak = abs(out[2 * i]);
        if (abs(out[2 * i + 1]) > peak) peak = abs(out[2 * i + 1]);
        st.read = (st.read + 1) % st.frames;
        st.queued--;
        st.played++;
    }
    u64 event = st.started ? st.event : 0;
    u64 now = rcn_now_ns();
    if (now - st.report_ns >= 5000000000ull) {
        if (st.report_ns) rcn_log("rcn : son %llu rames jouees, %llu manquantes, %u en attente", (unsigned long long)st.delivered, (unsigned long long)st.starved, st.queued);
        if (log_audio) rcn_log("rcn : son, crete du dernier tampon %d", peak);
        st.report_ns = now;
        st.starved = st.delivered = 0;
    }
    pthread_mutex_unlock(&st.lock);
    if (event) rcn_event_signal(st.proc, event);
}

static void *clock_thread(void *arg) {
    (void)arg;
    rcn_thread_default_affinity();
    int16_t scratch[2 * 480];
    for (;;) {
        rcn_sleep_ms(10);
        fill(NULL, scratch, (int)(st.rate / 100 < 480 ? st.rate / 100 : 480));
    }
    return NULL;
}

static void start_host(RcnCtx *ctx) {
    if (st.host_state) return;
    const RcnHost *host = ctx->proc->host;
    if (host->audio_open && host->audio_open(host->user, (int)st.rate, fill, NULL) == 0) {
        st.host_state = 1;
        rcn_log("rcn : son WASAPI -> hote, %u Hz, %u voies, %u bits%s, tampon %u rames", st.rate, st.channels, st.bits, st.is_float ? " flottants" : "", st.frames);
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
    u32 n = (u32)A1;
    pthread_mutex_lock(&st.lock);
    u32 room = st.frames - st.queued;
    pthread_mutex_unlock(&st.lock);
    if (n > room) {
        RET(AUDCLNT_E_BUFFER_TOO_LARGE);
        return;
    }
    if (n > st.staging_frames) {
        free(st.staging);
        st.staging = calloc(n ? n : 1, st.block);
        st.staging_frames = st.staging ? n : 0;
    }
    wr64(A2, (u64)(uintptr_t)st.staging);
    RET(st.staging ? S_OK : 0x8007000Eu);
}
// ReleaseBuffer(this, frames, flags)
static void rc_ReleaseBuffer(RcnCtx *ctx) {
    u32 n = (u32)A1, silent = ((u32)A2 & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    static int log_audio = -1;
    if (log_audio < 0) log_audio = getenv("RCN_AUDIO_LOG") != NULL;
    if (log_audio) {
        static u32 last;
        u32 now = (u32)(rcn_now_ns() / 1000000);
        if (now - last >= 3000) {
            last = now;
            int peak = 0;
            if (!silent)
                for (u32 i = 0; i < n * st.block && st.staging; i += 4) {
                    float f;
                    memcpy(&f, st.staging + i, 4);
                    int v = (int)(f * 32767.0f);
                    if (v < 0) v = -v;
                    if (v > peak) peak = v;
                }
            rcn_log("rcn : ReleaseBuffer %u rames, indicateurs %x, crete du tampon %d", n, (u32)A2, peak);
        }
    }
    pthread_mutex_lock(&st.lock);
    for (u32 i = 0; i < n && st.queued < st.frames; i++) {
        u32 at = (st.read + st.queued) % st.frames;
        if (silent) memset(st.ring + (size_t)at * st.block, 0, st.block);
        else memcpy(st.ring + (size_t)at * st.block, st.staging + (size_t)i * st.block, st.block);
        st.queued++;
    }
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static const Method render_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetBuffer", rc_GetBuffer},           {"ReleaseBuffer", rc_ReleaseBuffer},
};

// --- IAudioClock ---

static void clk_GetFrequency(RcnCtx *ctx) {
    wr64(A1, st.rate);
    RET(S_OK);
}
static void clk_GetPosition(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    u64 p = st.played;
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
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetFrequency", clk_GetFrequency},     {"GetPosition", clk_GetPosition}, {"GetCharacteristics", clk_GetCharacteristics},
};

// --- IAudioClient ---

static u64 render_cache, clock_cache;
// Initialize(this, mode, flags, duration, period, &format, &session)
static void ac_Initialize(RcnCtx *ctx) {
    u32 flags = (u32)A2;
    i64 duration = (i64)A3;
    u64 f = STK(5);
    u16 tag = rd16(f), channels = rd16(f + 2), bits = rd16(f + 14), block = rd16(f + 12);
    u32 rate = rd32(f + 4);
    int is_float = tag == 3 || (tag == 0xFFFE && rd32(f + 24) == 3);
    if (!channels || !block || !rate) {
        RET(0x88890008u); // AUDCLNT_E_UNSUPPORTED_FORMAT
        return;
    }
    // Tampon : la durée demandée, 40 ms au moins.
    u32 frames = (u32)((duration > 0 ? duration : 400000) * (i64)rate / 10000000);
    if (frames < rate / 25) frames = rate / 25;
    pthread_mutex_lock(&st.lock);
    st.proc = ctx->proc;
    st.channels = channels;
    st.bits = bits;
    st.is_float = is_float;
    st.block = block;
    st.rate = rate;
    free(st.ring);
    st.frames = frames;
    st.ring = calloc(frames, block);
    st.read = st.queued = 0;
    st.played = 0;
    st.initialized = st.ring != NULL;
    st.event = 0;
    pthread_mutex_unlock(&st.lock);
    (void)flags;
    RET(st.initialized ? S_OK : 0x8007000Eu);
}
static void ac_GetBufferSize(RcnCtx *ctx) {
    if (!st.initialized) {
        RET(AUDCLNT_E_NOT_INITIALIZED);
        return;
    }
    wr32(A1, st.frames);
    RET(S_OK);
}
static void ac_GetStreamLatency(RcnCtx *ctx) {
    wr64(A1, 100000);
    RET(S_OK);
}
static void ac_GetCurrentPadding(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    wr32(A1, st.queued);
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
    start_host(ctx);
    pthread_mutex_lock(&st.lock);
    st.started = 1;
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static void ac_Stop(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    st.started = 0;
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static void ac_Reset(RcnCtx *ctx) {
    pthread_mutex_lock(&st.lock);
    st.read = st.queued = 0;
    pthread_mutex_unlock(&st.lock);
    RET(S_OK);
}
static void ac_SetEventHandle(RcnCtx *ctx) {
    st.event = A1;
    RET(S_OK);
}
// Tables du volume et de la session (définies plus bas, avec IMMDevice).
static const Method simple_volume_methods[], session_methods[];
static u64 simple_cache, session_cache;
// GetService(this, &iid, &object)
static void ac_GetService(RcnCtx *ctx) {
    u32 id = A1 ? rd32(A1) : 0;
    u64 object = 0;
    if (id == 0xF294ACFCu) {
        object = com_new(ctx, vtable(ctx, "IAudioRenderClient", render_methods, 5, &render_cache), 0);
    } else if (id == 0xCD63314Fu) {
        object = com_new(ctx, vtable(ctx, "IAudioClock", clock_methods, 6, &clock_cache), 0);
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
    {"Release", com_Release},               {"Initialize", ac_Initialize},
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
        object = com_new(ctx, vtable(ctx, "IAudioClient", client_methods, 21, &client_cache), 0);
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
