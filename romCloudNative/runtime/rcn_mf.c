// RomCloud Native : Media Foundation (mf.dll, mfplat.dll, mfplay.dll, mfreadwrite.dll) des jeux 64 bits.
// Le lecteur vidéo d'Unreal Engine (WmfMedia) n'est actif que si ces bibliothèques se chargent ; sans lui,
// un jeu qui attend la fin d'une vidéo (écran de chargement) attend toujours.
//
// Annoncée aux jeux (rcn_kernel32.c) : sans elle, la logique de Clair Obscur ne quittait jamais l'écran de
// chargement du menu (actions du menu bloquées). Aucun film n'est décodé : la session accepte tout, le
// résolveur refuse chaque film, les autres fonctions notent leur appel et échouent (E_NOTIMPL).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_internal.h"

enum { S_OK = 0, E_NOTIMPL = 0x80004001u, E_POINTER = 0x80004003u };

static void mf_log(const char *what) {
    static int told;
    if (told++ < 200) rcn_log("media foundation : %s", what);
}
static void mf_Startup(RcnCtx *ctx) {
    mf_log("MFStartup");
    RET(S_OK);
}
static void mf_Shutdown(RcnCtx *ctx) { RET(S_OK); }

#define MF_FAIL(name)                              \
    static void mf_##name(RcnCtx *ctx) {           \
        mf_log(#name " (non fourni)");             \
        if (A0 == 0) (void)0;                      \
        RET(E_NOTIMPL);                            \
    }
MF_FAIL(MFCreateAlignedMemoryBuffer)
MF_FAIL(MFCreateAsyncResult)
MF_FAIL(MFCreateAttributes)
MF_FAIL(MFCreateAudioRendererActivate)
MF_FAIL(MFCreateDXGIDeviceManager)
MF_FAIL(MFCreateDeviceSource)
MF_FAIL(MFCreateEventQueue)
// --- Session et résolveur minimaux ---
// Le lecteur de films de Windows d'Unreal Engine (WindowsMoviePlayer, écrans de chargement) ne teste pas
// le code rendu par MFCreateMediaSession / MFCreateSourceResolver : il se sert du pointeur. On lui donne
// une session qui accepte tout et un résolveur qui refuse proprement chaque film (type de flux non pris
// en charge), comme Windows face à un fichier illisible : le jeu passe alors au film suivant, ou à la suite.
enum { MF_E_UNSUPPORTED_BYTESTREAM_TYPE = 0xC00D36C4u, MF_E_NO_EVENTS_AVAILABLE = 0xC00D3E80u };

typedef struct {
    const char *name;
    RcnFn fn;
} MfMethod;
static u64 mf_vtable(RcnCtx *ctx, const char *iface, const MfMethod *methods, u32 count, u64 *cache) {
    if (*cache) return *cache;
    u64 *table = rcn_galloc(ctx, count * 8, 1);
    for (u32 i = 0; table && i < count; i++) table[i] = rcn_thunk(ctx, iface, methods[i].name, methods[i].fn, 0);
    *cache = (u64)(uintptr_t)table;
    return *cache;
}
static u64 mf_object(RcnCtx *ctx, u64 table) {
    u8 *object = rcn_galloc(ctx, 16, 1);
    if (!object) return 0;
    memcpy(object, &table, 8);
    *(u32 *)(object + 8) = 1;
    return (u64)(uintptr_t)object;
}
static void mf_AddRef(RcnCtx *ctx) { RET(++*(u32 *)((u8 *)PTR(A0) + 8)); }
static void mf_Release(RcnCtx *ctx) {
    u32 *refs = (u32 *)((u8 *)PTR(A0) + 8);
    if (*refs) --*refs;
    RET(*refs);
}
static void mf_QueryInterface(RcnCtx *ctx) {
    if (!A2) {
        RET(E_POINTER);
        return;
    }
    wr64(A2, A0);
    mf_AddRef(ctx);
    RET(S_OK);
}
static void mf_Ok(RcnCtx *ctx) { RET(S_OK); }
static void mf_NotImpl(RcnCtx *ctx) { RET(E_NOTIMPL); }
static void mf_NoEvent(RcnCtx *ctx) {
    if (A2) wr64(A2, 0);
    RET(MF_E_NO_EVENTS_AVAILABLE);
}
static void mf_NullOut1(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    RET(E_NOTIMPL);
}
static void mf_ZeroOut1(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(S_OK);
}
// Résolveur : CreateObjectFromURL(url, flags, props, &type, &objet) et CreateObjectFromByteStream(flux, url,
// flags, props, &type, &objet) refusent ; les variantes asynchrones aussi.
static void mf_RefuseUrl(RcnCtx *ctx) {
    mf_log("film refusé (aucun décodeur)");
    if (A3) wr32(A3, 0);
    if (STK(4)) wr64(STK(4), 0);
    RET(MF_E_UNSUPPORTED_BYTESTREAM_TYPE);
}
static void mf_RefuseStream(RcnCtx *ctx) {
    mf_log("film refusé (aucun décodeur)");
    if (STK(4)) wr32(STK(4), 0);
    if (STK(5)) wr64(STK(5), 0);
    RET(MF_E_UNSUPPORTED_BYTESTREAM_TYPE);
}
static void mf_Refuse(RcnCtx *ctx) { RET(MF_E_UNSUPPORTED_BYTESTREAM_TYPE); }

// --- Événements de la session ---
// BeginGetEvent(rappel, état) garde le rappel du lecteur (IMFAsyncCallback) ; Close, Start et Stop
// annoncent leur événement (MESessionClosed, MESessionStarted puis MESessionEnded, MESessionStopped) en
// appelant aussitôt son Invoke(résultat), et EndGetEvent(résultat, &événement) rend l'événement en attente.
// Unreal Engine attend MESessionClosed après Close : sans lui, l'écran de chargement attendait toujours.
enum { MESessionStarted = 103, MESessionStopped = 105, MESessionClosed = 106, MESessionEnded = 107 };
typedef struct {
    u64 callback, state;
    u32 pending[8];
    u32 head, count;
} MfSession;
#define MF_DATA(type, object) ((type *)((u8 *)PTR(object) + 16))

static u64 mf_object_data(RcnCtx *ctx, u64 table, size_t size) {
    u8 *object = rcn_galloc(ctx, 16 + size, 1);
    if (!object) return 0;
    memcpy(object, &table, 8);
    *(u32 *)(object + 8) = 1;
    return (u64)(uintptr_t)object;
}
// IMFAsyncResult : état du lecteur seulement.
static void ar_GetState(RcnCtx *ctx) {
    u64 state = *MF_DATA(u64, A0);
    if (A1) wr64(A1, state);
    RET(state ? S_OK : 0x80004005u); // E_FAIL sans état
}
static void ar_GetObject(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    RET(E_POINTER);
}
static void ar_GetStateNoAddRef(RcnCtx *ctx) { RET(*MF_DATA(u64, A0)); }
static u64 async_result(RcnCtx *ctx, u64 state) {
    static const MfMethod methods[] = {
        {"QueryInterface", mf_QueryInterface}, {"AddRef", mf_AddRef}, {"Release", mf_Release}, {"GetState", ar_GetState},
        {"GetStatus", mf_Ok}, {"SetStatus", mf_Ok}, {"GetObject", ar_GetObject}, {"GetStateNoAddRef", ar_GetStateNoAddRef},
    };
    static u64 cache;
    u64 object = mf_object_data(ctx, mf_vtable(ctx, "IMFAsyncResult", methods, 8, &cache), 8);
    if (object) *MF_DATA(u64, object) = state;
    return object;
}
// IMFMediaEvent : IMFAttributes vide (30 méthodes) puis GetType, GetExtendedType, GetStatus, GetValue.
enum { MF_E_ATTRIBUTENOTFOUND = 0xC00D36E6u };
static void ev_NotFound(RcnCtx *ctx) { RET(MF_E_ATTRIBUTENOTFOUND); }
static void ev_GetType(RcnCtx *ctx) {
    if (A1) wr32(A1, *MF_DATA(u32, A0));
    RET(S_OK);
}
static void ev_Zero16(RcnCtx *ctx) {
    if (A1) memset(PTR(A1), 0, 16);
    RET(S_OK);
}
static void ev_Zero24(RcnCtx *ctx) {
    if (A1) memset(PTR(A1), 0, 24);
    RET(S_OK);
}
static u64 media_event(RcnCtx *ctx, u32 type) {
    static MfMethod methods[37];
    static u64 cache;
    if (!cache) {
        static const char *const attrs[30] = {"GetItem", "GetItemType", "CompareItem", "Compare", "GetUINT32", "GetUINT64",
                                              "GetDouble", "GetGUID", "GetStringLength", "GetString", "GetAllocatedString",
                                              "GetBlobSize", "GetBlob", "GetAllocatedBlob", "GetUnknown", "SetItem", "DeleteItem",
                                              "DeleteAllItems", "SetUINT32", "SetUINT64", "SetDouble", "SetGUID", "SetString",
                                              "SetBlob", "SetUnknown", "LockStore", "UnlockStore", "GetCount", "GetItemByIndex",
                                              "CopyAllItems"};
        methods[0] = (MfMethod){"QueryInterface", mf_QueryInterface};
        methods[1] = (MfMethod){"AddRef", mf_AddRef};
        methods[2] = (MfMethod){"Release", mf_Release};
        for (int i = 0; i < 30; i++) {
            RcnFn fn = ev_NotFound;
            if (!strncmp(attrs[i], "Set", 3) || !strcmp(attrs[i], "DeleteItem") || !strcmp(attrs[i], "DeleteAllItems") ||
                !strcmp(attrs[i], "LockStore") || !strcmp(attrs[i], "UnlockStore") || !strcmp(attrs[i], "CopyAllItems"))
                fn = mf_Ok;
            if (!strcmp(attrs[i], "GetCount")) fn = mf_ZeroOut1;
            methods[3 + i] = (MfMethod){attrs[i], fn};
        }
        methods[33] = (MfMethod){"GetType", ev_GetType};
        methods[34] = (MfMethod){"GetExtendedType", ev_Zero16};
        methods[35] = (MfMethod){"GetStatus", mf_ZeroOut1};
        methods[36] = (MfMethod){"GetValue", ev_Zero24};
    }
    u64 object = mf_object_data(ctx, mf_vtable(ctx, "IMFMediaEvent", methods, 37, &cache), 8);
    if (object) *MF_DATA(u32, object) = type;
    return object;
}
// Événement mis en file puis remis : Invoke(rappel, résultat) du lecteur, qui appelle EndGetEvent.
static void session_post(RcnCtx *ctx, u64 session, u32 type) {
    MfSession *s = MF_DATA(MfSession, session);
    if (s->count < 8) s->pending[(s->head + s->count++) % 8] = type;
    u64 callback = s->callback;
    if (!callback) return;
    s->callback = 0; // un rappel par BeginGetEvent, comme Media Foundation
    u64 result = async_result(ctx, s->state);
    u64 invoke = rd64(rd64(callback) + 4 * 8); // IMFAsyncCallback : QI, AddRef, Release, GetParameters, Invoke
    rcn_call_guest(ctx, invoke, callback, result, 0, 0);
}
static void ms_BeginGetEvent(RcnCtx *ctx) {
    MfSession *s = MF_DATA(MfSession, A0);
    s->callback = A1, s->state = A2;
    // Événement déjà en attente : remis tout de suite.
    if (s->count) {
        u32 type = s->pending[s->head];
        s->head = (s->head + 1) % 8, s->count--;
        session_post(ctx, A0, type);
    }
    RET(S_OK);
}
static void ms_EndGetEvent(RcnCtx *ctx) {
    MfSession *s = MF_DATA(MfSession, A0);
    if (!s->count) {
        mf_NoEvent(ctx);
        return;
    }
    u32 type = s->pending[s->head];
    s->head = (s->head + 1) % 8, s->count--;
    u64 event = media_event(ctx, type);
    if (A2) wr64(A2, event);
    RET(event ? S_OK : 0x8007000Eu);
}
static void ms_SetTopology(RcnCtx *ctx) { RET(S_OK); }
static void ms_Start(RcnCtx *ctx) {
    mf_log("session : Start");
    session_post(ctx, A0, MESessionStarted);
    session_post(ctx, A0, MESessionEnded);
    RET(S_OK);
}
static void ms_Stop(RcnCtx *ctx) {
    session_post(ctx, A0, MESessionStopped);
    RET(S_OK);
}
static void ms_Close(RcnCtx *ctx) {
    mf_log("session : Close");
    session_post(ctx, A0, MESessionClosed);
    RET(S_OK);
}
static void ms_Shutdown(RcnCtx *ctx) { RET(S_OK); }
static void mf_MFCreateMediaSession(RcnCtx *ctx) {
    static const MfMethod methods[] = {
        {"QueryInterface", mf_QueryInterface}, {"AddRef", mf_AddRef}, {"Release", mf_Release},
        {"GetEvent", mf_NoEvent}, {"BeginGetEvent", ms_BeginGetEvent}, {"EndGetEvent", ms_EndGetEvent}, {"QueueEvent", mf_Ok},
        {"SetTopology", ms_SetTopology}, {"ClearTopologies", mf_Ok}, {"Start", ms_Start}, {"Pause", mf_Ok}, {"Stop", ms_Stop},
        {"Close", ms_Close}, {"Shutdown", ms_Shutdown}, {"GetClock", mf_NullOut1}, {"GetSessionCapabilities", mf_ZeroOut1},
        {"GetFullTopology", mf_NotImpl},
    };
    static u64 cache;
    mf_log("MFCreateMediaSession");
    u64 out = A1;
    u64 object = mf_object_data(ctx, mf_vtable(ctx, "IMFMediaSession", methods, sizeof methods / sizeof methods[0], &cache), sizeof(MfSession));
    if (out) wr64(out, object);
    RET(object ? S_OK : 0x8007000Eu); // E_OUTOFMEMORY
}
static void mf_MFCreateSourceResolver(RcnCtx *ctx) {
    static const MfMethod methods[] = {
        {"QueryInterface", mf_QueryInterface}, {"AddRef", mf_AddRef}, {"Release", mf_Release},
        {"CreateObjectFromURL", mf_RefuseUrl}, {"CreateObjectFromByteStream", mf_RefuseStream},
        {"BeginCreateObjectFromURL", mf_Refuse}, {"EndCreateObjectFromURL", mf_Refuse},
        {"BeginCreateObjectFromByteStream", mf_Refuse}, {"EndCreateObjectFromByteStream", mf_Refuse},
        {"CancelObjectCreation", mf_Ok},
    };
    static u64 cache;
    mf_log("MFCreateSourceResolver");
    u64 out = A0;
    u64 object = mf_object(ctx, mf_vtable(ctx, "IMFSourceResolver", methods, sizeof methods / sizeof methods[0], &cache));
    if (out) wr64(out, object);
    RET(object ? S_OK : 0x8007000Eu);
}
MF_FAIL(MFCreateMediaType)
MF_FAIL(MFCreateSample)
MF_FAIL(MFCreateSampleGrabberSinkActivate)
MF_FAIL(MFCreateSinkWriterFromURL)
MF_FAIL(MFCreateTopology)
MF_FAIL(MFCreateTopologyNode)
MF_FAIL(MFGetService)
MF_FAIL(MFCreateSourceReaderFromURL)
MF_FAIL(MFCreateMemoryBuffer)

#define E(name) {#name, mf_##name, 0}
const RcnShim rcn_mf_x64[] = {
    {"MFStartup", mf_Startup, 0},
    {"MFShutdown", mf_Shutdown, 0},
    E(MFCreateAlignedMemoryBuffer),
    E(MFCreateAsyncResult),
    E(MFCreateAttributes),
    E(MFCreateAudioRendererActivate),
    E(MFCreateDXGIDeviceManager),
    E(MFCreateDeviceSource),
    E(MFCreateEventQueue),
    {"MFCreateMediaSession", mf_MFCreateMediaSession, 0},
    E(MFCreateMediaType),
    E(MFCreateSample),
    E(MFCreateSampleGrabberSinkActivate),
    E(MFCreateSinkWriterFromURL),
    {"MFCreateSourceResolver", mf_MFCreateSourceResolver, 0},
    E(MFCreateTopology),
    E(MFCreateTopologyNode),
    E(MFGetService),
    E(MFCreateSourceReaderFromURL),
    E(MFCreateMemoryBuffer),
    {NULL, NULL, 0},
};
