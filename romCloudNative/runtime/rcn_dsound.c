// RomCloud Native : DirectSound, pour les jeux 32 bits.
//
// Un tampon de son est un bloc d'échantillons dans la mémoire du jeu (8 bits non signés ou 16 bits
// signés, mono ou stéréo, à sa propre fréquence). L'hôte demande des échantillons depuis son thread de
// son (RcnHost.audio_open) : le mélangeur additionne alors les tampons en lecture, chacun rééchantillonné
// à 44,1 kHz (échantillon le plus proche) avec son volume et sa balance.
// Sans sortie de son chez l'hôte, la création du périphérique échoue (« aucun pilote »), ce que les
// jeux prennent pour une machine sans carte son.
//
// Pas fournis : son 3D, effets, notifications de position, capture.
#include <math.h>
#include <stdlib.h>

#include "rcn_gui.h"

enum {
    DS_OK = 0,
    DSERR_GENERIC = 0x80004005u,
    DSERR_INVALIDPARAM = 0x80070057u,
    DSERR_NODRIVER = 0x88780078u,
    DSBCAPS_PRIMARYBUFFER = 0x1,
    DSBPLAY_LOOPING = 0x1,
    DSBSTATUS_PLAYING = 0x1,
    DSBSTATUS_LOOPING = 0x4,
    DSBLOCK_FROMWRITECURSOR = 0x1,
    DSBLOCK_ENTIREBUFFER = 0x2,
    MIX_RATE = 44100,
    MAX_BUFFERS = 256,
    DATA_HEADER = 16, // compteur de partage devant les échantillons (tampons dupliqués)
};

#define U32(n) ((u32)rcn_arg(ctx, n))

typedef struct {
    u32 data;  // échantillons, dans la mémoire du jeu (0 : tampon primaire)
    u32 bytes; // taille des échantillons
    u32 channels, bits, rate;
    u32 frequency; // fréquence de lecture (celle du format par défaut)
    i32 volume, pan; // centièmes de décibel : 0 à -10000 ; -10000 (gauche) à 10000 (droite)
    float gain_left, gain_right;
    u32 playing, looping;
    u64 position; // image de lecture, en virgule fixe 32.32
    u32 flags;
} Sound;

// Tampons vivants, lus par le mélangeur depuis le thread de son de l'hôte.
static Sound *sounds[MAX_BUFFERS];
static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
static int audio_opened;

static u32 frame_bytes(const Sound *s) { return s->channels * (s->bits / 8); }

static void update_gains(Sound *s) {
    float gain = s->volume <= -10000 ? 0 : powf(10.0f, (float)s->volume / 2000.0f);
    s->gain_left = s->pan > 0 ? gain * powf(10.0f, (float)-s->pan / 2000.0f) : gain;
    s->gain_right = s->pan < 0 ? gain * powf(10.0f, (float)s->pan / 2000.0f) : gain;
}

// Appelé par l'hôte : `frames` échantillons stéréo de 16 bits.
static void mix(void *context, int16_t *out, int frames) {
    (void)context;
    memset(out, 0, (size_t)frames * 4);
    pthread_mutex_lock(&mixer_lock);
    for (int b = 0; b < MAX_BUFFERS; b++) {
        Sound *s = sounds[b];
        if (!s || !s->playing || !s->data || s->frequency == 0) continue;
        u32 size = frame_bytes(s);
        u64 total = size ? s->bytes / size : 0;
        if (total == 0) continue;
        u64 step = ((u64)s->frequency << 32) / MIX_RATE;
        const u8 *data = PTR(s->data);
        for (int i = 0; i < frames; i++) {
            u64 index = s->position >> 32;
            if (index >= total) {
                if (!s->looping) {
                    s->playing = 0;
                    s->position = 0;
                    break;
                }
                s->position -= total << 32;
                index = s->position >> 32;
                if (index >= total) { // fréquence démesurée : on repart du début
                    s->position = 0;
                    index = 0;
                }
            }
            const u8 *frame = data + index * size;
            i32 left, right;
            if (s->bits == 8) {
                left = ((i32)frame[0] - 128) << 8;
                right = s->channels == 2 ? ((i32)frame[1] - 128) << 8 : left;
            } else {
                left = (i16)(frame[0] | frame[1] << 8);
                right = s->channels == 2 ? (i16)(frame[2] | frame[3] << 8) : left;
            }
            i32 l = out[i * 2] + (i32)((float)left * s->gain_left);
            i32 r = out[i * 2 + 1] + (i32)((float)right * s->gain_right);
            out[i * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            out[i * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            s->position += step;
        }
    }
    pthread_mutex_unlock(&mixer_lock);
}

static void register_sound(Sound *s, int add) {
    pthread_mutex_lock(&mixer_lock);
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (sounds[i] == (add ? NULL : s)) {
            sounds[i] = add ? s : NULL;
            break;
        }
    }
    pthread_mutex_unlock(&mixer_lock);
}

// --- IDirectSoundBuffer ---

static void b_Ok(RcnCtx *ctx) { RET(DS_OK); }
static void b_QueryInterface(RcnCtx *ctx) {
    COM_REFS(A0)++;
    wr32(A2, U32(0));
    RET(DS_OK);
}
static void b_AddRef(RcnCtx *ctx) { RET(++COM_REFS(A0)); }
static void b_Release(RcnCtx *ctx) {
    u32 object = U32(0);
    u32 refs = --COM_REFS(object);
    if (refs == 0) {
        Sound *s = COM_DATA(Sound, object);
        register_sound(s, 0);
        // Échantillons partagés avec les tampons dupliqués : libérés avec le dernier.
        if (s->data && --*(u32 *)PTR(s->data - DATA_HEADER) == 0) rcn_gfree(ctx, PTR(s->data - DATA_HEADER));
        rcn_gfree(ctx, PTR(object));
    }
    RET(refs);
}
// GetCaps(this, &caps) : DSBCAPS { taille, drapeaux, octets, … }.
static void b_GetCaps(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    wr32(A1 + 4, s->flags);
    wr32(A1 + 8, s->bytes);
    RET(DS_OK);
}
// GetCurrentPosition(this, &play, &write) : en octets.
static void b_GetCurrentPosition(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    pthread_mutex_lock(&mixer_lock);
    u32 play = s->bytes ? (u32)((s->position >> 32) * frame_bytes(s)) % s->bytes : 0;
    pthread_mutex_unlock(&mixer_lock);
    if (A1) wr32(A1, play);
    // Écriture sûre un peu après la lecture (une vingtaine de millisecondes).
    if (A2) wr32(A2, s->bytes ? (play + frame_bytes(s) * (s->frequency / 50)) % s->bytes : 0);
    RET(DS_OK);
}
// GetFormat(this, &format, size, &written) : WAVEFORMATEX (18 octets).
static void b_GetFormat(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    u8 format[18] = {0};
    u32 block = frame_bytes(s);
    format[0] = 1; // PCM
    format[2] = (u8)s->channels;
    memcpy(format + 4, &s->rate, 4);
    u32 byte_rate = s->rate * block;
    memcpy(format + 8, &byte_rate, 4);
    format[12] = (u8)block;
    format[14] = (u8)s->bits;
    u32 size = U32(2) < 18 ? U32(2) : 18;
    if (A1) memcpy(PTR(A1), format, size);
    if (A3) wr32(A3, 18);
    RET(DS_OK);
}
static void b_GetVolume(RcnCtx *ctx) {
    wr32(A1, (u32)COM_DATA(Sound, A0)->volume);
    RET(DS_OK);
}
static void b_GetPan(RcnCtx *ctx) {
    wr32(A1, (u32)COM_DATA(Sound, A0)->pan);
    RET(DS_OK);
}
static void b_GetFrequency(RcnCtx *ctx) {
    wr32(A1, COM_DATA(Sound, A0)->frequency);
    RET(DS_OK);
}
static void b_GetStatus(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    wr32(A1, s->playing ? DSBSTATUS_PLAYING | (s->looping ? DSBSTATUS_LOOPING : 0) : 0);
    RET(DS_OK);
}
// Lock(this, offset, bytes, &part1, &size1, &part2, &size2, flags) : deux morceaux si la zone demandée
// dépasse la fin du tampon (il est circulaire).
static void b_Lock(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    u32 offset = U32(1), bytes = U32(2), flags = U32(7);
    if (!s->data || !s->bytes) {
        RET(DSERR_INVALIDPARAM);
        return;
    }
    if (flags & DSBLOCK_FROMWRITECURSOR) offset = (u32)((s->position >> 32) * frame_bytes(s));
    if (flags & DSBLOCK_ENTIREBUFFER) bytes = s->bytes;
    offset %= s->bytes;
    if (bytes > s->bytes) bytes = s->bytes;
    u32 first = bytes < s->bytes - offset ? bytes : s->bytes - offset;
    wr32(A3, s->data + offset);
    wr32(rcn_arg(ctx, 4), first);
    if (rcn_arg(ctx, 5)) wr32(rcn_arg(ctx, 5), bytes > first ? s->data : 0);
    if (rcn_arg(ctx, 6)) wr32(rcn_arg(ctx, 6), bytes - first);
    RET(DS_OK);
}
// Play(this, reserved, priority, flags) : un tampon déjà en lecture continue où il en est.
static void b_Play(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    pthread_mutex_lock(&mixer_lock);
    s->looping = (U32(3) & DSBPLAY_LOOPING) != 0;
    s->playing = 1;
    pthread_mutex_unlock(&mixer_lock);
    RET(DS_OK);
}
static void b_Stop(RcnCtx *ctx) {
    COM_DATA(Sound, A0)->playing = 0;
    RET(DS_OK);
}
// SetCurrentPosition(this, bytes)
static void b_SetCurrentPosition(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    u32 size = frame_bytes(s);
    pthread_mutex_lock(&mixer_lock);
    s->position = size ? (u64)(U32(1) / size) << 32 : 0;
    pthread_mutex_unlock(&mixer_lock);
    RET(DS_OK);
}
static void b_SetVolume(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    i32 volume = (i32)U32(1);
    s->volume = volume > 0 ? 0 : volume < -10000 ? -10000 : volume;
    update_gains(s);
    RET(DS_OK);
}
static void b_SetPan(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    i32 pan = (i32)U32(1);
    s->pan = pan > 10000 ? 10000 : pan < -10000 ? -10000 : pan;
    update_gains(s);
    RET(DS_OK);
}
// SetFrequency(this, frequency) : 0 rétablit celle du format.
static void b_SetFrequency(RcnCtx *ctx) {
    Sound *s = COM_DATA(Sound, A0);
    u32 frequency = U32(1);
    s->frequency = frequency ? frequency : s->rate;
    RET(DS_OK);
}

#define M(name, fn, args) {#name, fn, args}
static const RcnMethod BUFFER_METHODS[] = {
    M(QueryInterface, b_QueryInterface, 3), M(AddRef, b_AddRef, 1), M(Release, b_Release, 1), M(GetCaps, b_GetCaps, 2),
    M(GetCurrentPosition, b_GetCurrentPosition, 3), M(GetFormat, b_GetFormat, 4), M(GetVolume, b_GetVolume, 2),
    M(GetPan, b_GetPan, 2), M(GetFrequency, b_GetFrequency, 2), M(GetStatus, b_GetStatus, 2),
    M(Initialize, b_Ok, 3), M(Lock, b_Lock, 8), M(Play, b_Play, 4), M(SetCurrentPosition, b_SetCurrentPosition, 2),
    M(SetFormat, b_Ok, 2), M(SetVolume, b_SetVolume, 2), M(SetPan, b_SetPan, 2), M(SetFrequency, b_SetFrequency, 2),
    M(Stop, b_Stop, 1), M(Unlock, b_Ok, 5), M(Restore, b_Ok, 1),
};

static u64 new_buffer(RcnCtx *ctx, const Sound *model) {
    u64 object = rcn_com_new(ctx, "IDirectSoundBuffer", BUFFER_METHODS, sizeof BUFFER_METHODS / sizeof BUFFER_METHODS[0], sizeof(Sound));
    if (!object) return 0;
    Sound *s = COM_DATA(Sound, object);
    *s = *model;
    s->playing = 0;
    s->position = 0;
    update_gains(s);
    register_sound(s, 1);
    return object;
}

// --- IDirectSound ---

// CreateSoundBuffer(this, &desc, &buffer, outer)
// DSBUFFERDESC { taille, drapeaux, octets, réservé, &format } ; WAVEFORMATEX { type, canaux, fréquence,
// octets par seconde, octets par image, bits par échantillon }.
static void d_CreateSoundBuffer(RcnCtx *ctx) {
    u64 desc = A1;
    Sound model = {0};
    model.flags = rd32(desc + 4);
    model.bytes = rd32(desc + 8);
    u64 format = rd32(desc + 16);
    model.channels = 2;
    model.bits = 16;
    model.rate = MIX_RATE;
    if (format) {
        model.channels = rd16(format + 2);
        model.rate = rd32(format + 4);
        model.bits = rd16(format + 14);
    }
    if ((model.channels != 1 && model.channels != 2) || (model.bits != 8 && model.bits != 16) || model.rate == 0) {
        RET(DSERR_INVALIDPARAM);
        return;
    }
    model.frequency = model.rate;
    if (model.flags & DSBCAPS_PRIMARYBUFFER) {
        model.bytes = 0;
    } else {
        if (model.bytes == 0 || model.bytes > (64u << 20)) {
            RET(DSERR_INVALIDPARAM);
            return;
        }
        u8 *block = rcn_galloc(ctx, DATA_HEADER + model.bytes, 0);
        if (!block) {
            RET(DSERR_GENERIC);
            return;
        }
        *(u32 *)block = 1;
        // Silence : 0x80 en 8 bits non signés, 0 en 16 bits.
        memset(block + DATA_HEADER, model.bits == 8 ? 0x80 : 0, model.bytes);
        model.data = (u32)(uintptr_t)block + DATA_HEADER;
    }
    u64 object = new_buffer(ctx, &model);
    if (!object && model.data) rcn_gfree(ctx, PTR(model.data - DATA_HEADER));
    wr32(A2, (u32)object);
    RET(object ? DS_OK : DSERR_GENERIC);
}
// DuplicateSoundBuffer(this, original, &copy) : mêmes échantillons, lecture indépendante.
static void d_DuplicateSoundBuffer(RcnCtx *ctx) {
    Sound *original = COM_DATA(Sound, A1);
    u64 object = new_buffer(ctx, original);
    if (object && original->data) ++*(u32 *)PTR(original->data - DATA_HEADER);
    wr32(A2, (u32)object);
    RET(object ? DS_OK : DSERR_GENERIC);
}
// GetCaps(this, &caps) : DSCAPS — mélange logiciel, tous formats.
static void d_GetCaps(RcnCtx *ctx) {
    u32 size = rd32(A1);
    if (size >= 8 && size <= 256) {
        memset(PTR(A1 + 4), 0, size - 4);
        wr32(A1 + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x100 | 0x200 | 0x400 | 0x800); // primaire et secondaires : mono, stéréo, 8 et 16 bits
        if (size >= 16) {
            wr32(A1 + 8, 100);
            wr32(A1 + 12, 100000);
        }
    }
    RET(DS_OK);
}
static void d_GetSpeakerConfig(RcnCtx *ctx) {
    wr32(A1, 4); // DSSPEAKER_STEREO
    RET(DS_OK);
}
static void d_Release(RcnCtx *ctx) {
    u32 refs = COM_REFS(A0);
    if (refs) COM_REFS(A0) = --refs;
    RET(refs);
}

static const RcnMethod DSOUND_METHODS[] = {
    M(QueryInterface, b_QueryInterface, 3), M(AddRef, b_AddRef, 1), M(Release, d_Release, 1),
    M(CreateSoundBuffer, d_CreateSoundBuffer, 4), M(GetCaps, d_GetCaps, 2),
    M(DuplicateSoundBuffer, d_DuplicateSoundBuffer, 3), M(SetCooperativeLevel, b_Ok, 3), M(Compact, b_Ok, 1),
    M(GetSpeakerConfig, d_GetSpeakerConfig, 2), M(SetSpeakerConfig, b_Ok, 2), M(Initialize, b_Ok, 2),
};

// Ouvre la sortie de son de l'hôte, une fois. Renvoie 0 si elle n'existe pas.
static int open_audio(RcnCtx *ctx) {
    const RcnHost *host = ctx->proc->host;
    pthread_mutex_lock(&mixer_lock);
    if (audio_opened == 0) audio_opened = host->audio_open && host->audio_open(host->user, MIX_RATE, mix, NULL) == 0 ? 1 : -1;
    int ok = audio_opened > 0;
    pthread_mutex_unlock(&mixer_lock);
    return ok;
}

// DirectSoundCreate(guid, &dsound, outer) — importée par son numéro d'ordre (1).
static void ds_DirectSoundCreate(RcnCtx *ctx) {
    u64 object = open_audio(ctx) ? rcn_com_new(ctx, "IDirectSound", DSOUND_METHODS, sizeof DSOUND_METHODS / sizeof DSOUND_METHODS[0], 4) : 0;
    if (A1) wr32(A1, (u32)object);
    RET(object ? DS_OK : DSERR_NODRIVER);
}
// DirectSoundEnumerateA(callback, context) — numéro d'ordre 2 : le périphérique par défaut, si l'hôte a
// une sortie de son. La fonction du jeu reçoit (guid, description, module, contexte).
static void ds_DirectSoundEnumerateA(RcnCtx *ctx) {
    u32 callback = U32(0), context = U32(1);
    char *text = ctx->proc->host->audio_open ? rcn_galloc(ctx, 64, 1) : NULL;
    if (text) {
        strcpy(text, "Primary Sound Driver");
        u64 args[4] = {0, (u64)(uintptr_t)text, (u64)(uintptr_t)text + 32, context};
        rcn_call_guest_n(ctx, callback, 4, args);
        rcn_gfree(ctx, text);
    }
    RET(DS_OK);
}

const RcnShim rcn_dsound[] = {
    {"#1", ds_DirectSoundCreate, S32(3)},
    {"#2", ds_DirectSoundEnumerateA, S32(2)},
    {"DirectSoundCreate", ds_DirectSoundCreate, S32(3)},
    {"DirectSoundEnumerateA", ds_DirectSoundEnumerateA, S32(2)},
};
const size_t rcn_dsound_count = sizeof rcn_dsound / sizeof rcn_dsound[0];
