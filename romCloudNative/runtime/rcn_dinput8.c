// RomCloud Native : DirectInput 8 des jeux 64 bits (dinput8.dll). Le clavier rapporte les frappes du
// clavier de l'hôte (rcn_keyboard.c : clavier virtuel d'Android) ; souris et manettes ne rapportent rien
// (sur l'appareil, la manette passe par XInput, rcn_xinput.c).
//
// Objets COM 64 bits : [0] table virtuelle, [8] compteur de références.
#include <stdlib.h>

#include "rcn_internal.h"

enum { DI_OK = 0, E_POINTER = 0x80004003u, DIERR_NOTFOUND = 0x80070002u };

typedef struct {
    const char *name;
    RcnFn fn;
} Method;

static u64 vtable(RcnCtx *ctx, const char *iface, const Method *methods, u32 count, u64 *cache) {
    if (*cache) return *cache;
    u64 *table = rcn_galloc(ctx, count * 8, 1);
    for (u32 i = 0; table && i < count; i++) table[i] = rcn_thunk(ctx, iface, methods[i].name, methods[i].fn, 0);
    *cache = (u64)(uintptr_t)table;
    return *cache;
}
// Objet : [0] table, [8] références, [16] nature (1 : clavier), [20] curseur dans les frappes.
typedef struct {
    u32 keyboard, cursor;
} Device;
#define DEVICE(object) ((Device *)((u8 *)PTR(object) + 16))
u32 rcn_keyboard_dinput_open(void);
u32 rcn_keyboard_dinput_read(u32 *cursor, u64 out, u32 size, u32 max);
void rcn_keyboard_dinput_state(u8 *out);

static u64 com_new(RcnCtx *ctx, u64 table) {
    u8 *object = rcn_galloc(ctx, 16 + sizeof(Device), 1);
    if (!object) return 0;
    memcpy(object, &table, 8);
    *(u32 *)(object + 8) = 1;
    return (u64)(uintptr_t)object;
}
static void com_AddRef(RcnCtx *ctx) { RET(++*(u32 *)((u8 *)PTR(A0) + 8)); }
static void com_Release(RcnCtx *ctx) {
    u32 *refs = (u32 *)((u8 *)PTR(A0) + 8);
    if (*refs) --*refs;
    RET(*refs);
}
static void com_QueryInterface(RcnCtx *ctx) {
    if (!A2) {
        RET(E_POINTER);
        return;
    }
    wr64(A2, A0);
    com_AddRef(ctx);
    RET(DI_OK);
}
static void com_Ok(RcnCtx *ctx) { RET(DI_OK); }

// --- IDirectInputDevice8 ---

// GetCapabilities(&DIDEVCAPS) : branché, sans axe ni bouton annoncé.
static void dev_GetCapabilities(RcnCtx *ctx) {
    u64 caps = A1;
    if (!caps) {
        RET(E_POINTER);
        return;
    }
    u32 size = rd32(caps);
    if (size > 4) memset((u8 *)PTR(caps) + 4, 0, size - 4);
    if (size >= 8) wr32(caps + 4, 1); // DIDC_ATTACHED
    RET(DI_OK);
}
// GetDeviceState(taille, données) : clavier, touches enfoncées ; souris immobile.
static void dev_GetDeviceState(RcnCtx *ctx) {
    if (A2 && (u32)A1) memset(PTR(A2), 0, (u32)A1);
    if (DEVICE(A0)->keyboard && A2 && (u32)A1 >= 256) rcn_keyboard_dinput_state(PTR(A2));
    RET(DI_OK);
}
// GetDeviceData(taille d'un élément, éléments, &nombre, options) : frappes du clavier ; DIGDD_PEEK (1)
// laisse les frappes en file. Sans tableau : nombre seul.
static void dev_GetDeviceData(RcnCtx *ctx) {
    u32 size = (u32)A1, max = A3 ? rd32(A3) : 0, flags = (u32)rcn_arg(ctx, 4);
    u32 n = 0;
    Device *d = DEVICE(A0);
    if (d->keyboard && A2 && size >= 16 && max) {
        u32 cursor = d->cursor;
        n = rcn_keyboard_dinput_read(&cursor, A2, size, max);
        if (!(flags & 1)) d->cursor = cursor;
    }
    if (A3) wr32(A3, n);
    RET(DI_OK);
}
// GetDeviceInfo(&DIDEVICEINSTANCE) : tout à zéro sauf sa taille.
static void dev_GetDeviceInfo(RcnCtx *ctx) {
    if (A1) {
        u32 size = rd32(A1);
        if (size > 4) memset((u8 *)PTR(A1) + 4, 0, size - 4);
    }
    RET(DI_OK);
}
static void dev_GetObjectInfo(RcnCtx *ctx) { RET(DIERR_NOTFOUND); }
static void dev_GetProperty(RcnCtx *ctx) { RET(DIERR_NOTFOUND); }
static const Method device_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetCapabilities", dev_GetCapabilities}, {"EnumObjects", com_Ok}, {"GetProperty", dev_GetProperty},
    {"SetProperty", com_Ok}, {"Acquire", com_Ok}, {"Unacquire", com_Ok}, {"GetDeviceState", dev_GetDeviceState},
    {"GetDeviceData", dev_GetDeviceData}, {"SetDataFormat", com_Ok}, {"SetEventNotification", com_Ok},
    {"SetCooperativeLevel", com_Ok}, {"GetObjectInfo", dev_GetObjectInfo}, {"GetDeviceInfo", dev_GetDeviceInfo},
    {"RunControlPanel", com_Ok}, {"Initialize", com_Ok}, {"CreateEffect", dev_GetProperty}, {"EnumEffects", com_Ok},
    {"GetEffectInfo", dev_GetProperty}, {"GetForceFeedbackState", dev_GetProperty},
    {"SendForceFeedbackCommand", com_Ok}, {"EnumCreatedEffectObjects", com_Ok}, {"Escape", com_Ok}, {"Poll", com_Ok},
    {"SendDeviceData", com_Ok}, {"EnumEffectsInFile", com_Ok}, {"WriteEffectToFile", com_Ok},
    {"BuildActionMap", com_Ok}, {"SetActionMap", com_Ok}, {"GetImageInfo", dev_GetProperty},
};
static u64 device_cache;

// --- IDirectInput8 ---

// CreateDevice(guid, &device, extérieur) : clavier, souris ou manette, tous muets.
static void di_CreateDevice(RcnCtx *ctx) {
    if (!A2) {
        RET(E_POINTER);
        return;
    }
    u64 object = com_new(ctx, vtable(ctx, "IDirectInputDevice8", device_methods, 32, &device_cache));
    // GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000} : le clavier reçoit les frappes de l'hôte.
    if (object && A1 && rd32(A1) == 0x6F1D2B61u) {
        DEVICE(object)->keyboard = 1;
        DEVICE(object)->cursor = rcn_keyboard_dinput_open();
    }
    wr64(A2, object);
    RET(object ? DI_OK : 0x8007000Eu);
}
// EnumDevices(type, rappel, contexte, options) : aucun périphérique énuméré (le clavier et la souris
// système se créent directement par leur GUID).
static const Method dinput_methods[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"CreateDevice", di_CreateDevice}, {"EnumDevices", com_Ok}, {"GetDeviceStatus", com_Ok},
    {"RunControlPanel", com_Ok}, {"Initialize", com_Ok}, {"FindDevice", dev_GetProperty},
    {"EnumDevicesBySemantics", com_Ok}, {"ConfigureDevices", com_Ok},
};
static u64 dinput_cache;

// DirectInput8Create(instance, version, interface, &objet, extérieur)
static void d8_DirectInput8Create(RcnCtx *ctx) {
    if (!A3) {
        RET(E_POINTER);
        return;
    }
    u64 object = com_new(ctx, vtable(ctx, "IDirectInput8", dinput_methods, 11, &dinput_cache));
    wr64(A3, object);
    RET(object ? DI_OK : 0x8007000Eu);
}

const RcnShim rcn_dinput8[] = {
    {"DirectInput8Create", d8_DirectInput8Create, 0},
    {NULL, NULL, 0},
};
