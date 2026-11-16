// RomCloud Native : fonctions de dsound.dll pour les jeux 64 bits. XAudio 2.7 et plus anciens énumèrent
// les sorties par DirectSound (DirectSoundEnumerateW, GetDeviceID) puis jouent par un tampon DirectSound :
// une sortie, « RomCloud », du même GUID que le périphérique de rcn_wasapi.c. L'objet DirectSound et ses
// tampons sont ceux de rcn_dsound.c.
#include "rcn_internal.h"

enum { DS_OK = 0, DSERR_NODRIVER = 0x88780078u, DSERR_INVALIDPARAM = 0x80070057u };

void rcn_dsound_create(RcnCtx *ctx);

// {52434E00-0000-4000-8000-524F4D434C44} : GUID de la sortie (aussi PKEY_AudioEndpoint_GUID de rcn_wasapi.c).
static const u8 audio_guid[16] = {0x00, 0x4E, 0x43, 0x52, 0x00, 0x00, 0x00, 0x40, 0x80, 0x00, 0x52, 0x4F, 0x4D, 0x43, 0x4C, 0x44};
static const u16 primary_w[] = {'P', 'r', 'i', 'm', 'a', 'r', 'y', ' ', 'S', 'o', 'u', 'n', 'd', ' ', 'D', 'r', 'i', 'v', 'e', 'r', 0};
static const u16 name_w[] = {'R', 'o', 'm', 'C', 'l', 'o', 'u', 'd', 0};
static const u16 empty_w[] = {0};
static const char primary_a[] = "Primary Sound Driver", name_a[] = "RomCloud";

// DirectSoundEnumerateW / A(rappel, contexte) : le pilote principal (GUID nul), puis la sortie ; rien si
// l'hôte n'a pas de sortie de son.
static void enumerate(RcnCtx *ctx, int wide) {
    u64 cb = A0, user = A1;
    if (!cb) {
        RET(DSERR_INVALIDPARAM);
        return;
    }
    if (ctx->proc->host->audio_open) {
        u64 primary = (uintptr_t)(wide ? (const void *)primary_w : (const void *)primary_a);
        u64 name = (uintptr_t)(wide ? (const void *)name_w : (const void *)name_a);
        u64 empty = (uintptr_t)(wide ? (const void *)empty_w : (const void *)"");
        if ((u32)rcn_call_guest(ctx, cb, 0, primary, empty, user)) rcn_call_guest(ctx, cb, (uintptr_t)audio_guid, name, empty, user);
    }
    RET(DS_OK);
}
static void ds_DirectSoundEnumerateW(RcnCtx *ctx) { enumerate(ctx, 1); }
static void ds_DirectSoundEnumerateA(RcnCtx *ctx) { enumerate(ctx, 0); }
// Aucune entrée de son.
static void ds_NoCapture(RcnCtx *ctx) { RET(DS_OK); }
// GetDeviceID(&source, &destination) : sortie par défaut (DSDEVID_DefaultPlayback, …VoicePlayback, ou
// nul) ou la sortie elle-même → son GUID ; entrées (DSDEVID_DefaultCapture, …VoiceCapture) : aucune.
static void ds_GetDeviceID(RcnCtx *ctx) {
    u64 src = A0, dst = A1;
    if (!dst) {
        RET(DSERR_INVALIDPARAM);
        return;
    }
    if (src && (rd32(src) == 0xDEF00001u || rd32(src) == 0xDEF00003u)) {
        RET(DSERR_NODRIVER);
        return;
    }
    memcpy(PTR(dst), audio_guid, 16);
    RET(DS_OK);
}
static void ds_DirectSoundCreate(RcnCtx *ctx) { rcn_dsound_create(ctx); }
// DirectSoundCaptureCreate(8) : pas de micro.
static void ds_NoCaptureDevice(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    RET(DSERR_NODRIVER);
}

const RcnShim rcn_dsound_x64[] = {
    {"DirectSoundEnumerateW", ds_DirectSoundEnumerateW, 0},
    {"DirectSoundEnumerateA", ds_DirectSoundEnumerateA, 0},
    {"DirectSoundCaptureEnumerateW", ds_NoCapture, 0},
    {"DirectSoundCaptureEnumerateA", ds_NoCapture, 0},
    {"GetDeviceID", ds_GetDeviceID, 0},
    {"DirectSoundCreate", ds_DirectSoundCreate, 0},
    {"DirectSoundCreate8", ds_DirectSoundCreate, 0},
    // Rangs de dsound.dll : 1 Create, 2 EnumerateA, 3 EnumerateW, 6 CaptureCreate, 7 et 8 CaptureEnumerate A/W,
    // 9 GetDeviceID, 11 Create8, 12 CaptureCreate8.
    {"#1", ds_DirectSoundCreate, 0},
    {"#2", ds_DirectSoundEnumerateA, 0},
    {"#3", ds_DirectSoundEnumerateW, 0},
    {"#6", ds_NoCaptureDevice, 0},
    {"#7", ds_NoCapture, 0},
    {"#8", ds_NoCapture, 0},
    {"#9", ds_GetDeviceID, 0},
    {"#11", ds_DirectSoundCreate, 0},
    {"#12", ds_NoCaptureDevice, 0},
    {NULL, NULL, 0},
};
