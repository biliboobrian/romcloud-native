// RomCloud Native : Media Foundation (mf.dll, mfplat.dll, mfplay.dll, mfreadwrite.dll) des jeux 64 bits.
// Le lecteur vidéo d'Unreal Engine (WmfMedia) n'est actif que si ces bibliothèques se chargent ; sans lui,
// un jeu qui attend la fin d'une vidéo (écran de chargement) attend toujours.
//
// Pas encore annoncée aux jeux (absente de la liste des bibliothèques connues, rcn_kernel32.c) : sans
// lecture réelle, la déclarer présente ferait échouer plus tard les lecteurs qui la croient là.
// Première étape : MFStartup / MFShutdown réussissent, toute autre fonction note son appel et échoue
// proprement (E_NOTIMPL), pour relever ce que le lecteur demande.
#include <stdio.h>
#include <stdlib.h>

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
MF_FAIL(MFCreateMediaSession)
MF_FAIL(MFCreateMediaType)
MF_FAIL(MFCreateSample)
MF_FAIL(MFCreateSampleGrabberSinkActivate)
MF_FAIL(MFCreateSinkWriterFromURL)
MF_FAIL(MFCreateSourceResolver)
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
    E(MFCreateMediaSession),
    E(MFCreateMediaType),
    E(MFCreateSample),
    E(MFCreateSampleGrabberSinkActivate),
    E(MFCreateSinkWriterFromURL),
    E(MFCreateSourceResolver),
    E(MFCreateTopology),
    E(MFCreateTopologyNode),
    E(MFGetService),
    E(MFCreateSourceReaderFromURL),
    E(MFCreateMemoryBuffer),
    {NULL, NULL, 0},
};
