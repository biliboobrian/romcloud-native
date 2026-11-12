// RomCloud Native : DirectInput 7, pour les jeux 32 bits.
//
// DirectInput : la manette de l'hôte est présentée au jeu à la fois comme une manette (deux axes,
// huit boutons, une croix) et comme un clavier, pour les jeux qui ne lisent que lui :
//   croix / stick → flèches ; A → Z ; B → X ; X → C ; Y → D ; Start → Entrée ; Select → Échap.
// La souris existe mais ne bouge pas.
#include <stdlib.h>

#include "rcn_gui.h"

enum {
    DI_OK = 0,
    DIERR_GENERIC = 0x80004005u,
    DEVICE_KEYBOARD = 1,
    DEVICE_MOUSE = 2,
    DEVICE_JOYSTICK = 3,
    DIDEVTYPE_MOUSE = 2,
    DIDEVTYPE_KEYBOARD = 3,
    DIDEVTYPE_JOYSTICK = 4,
    DIPROP_RANGE = 4,
};

#define U32(n) ((u32)rcn_arg(ctx, n))

// Identifiants des périphériques système : {6F1D2Bxx-D5A0-11CF-BFC7-444553540000}.
static const u8 GUID_TAIL[12] = {0xA0, 0xD5, 0xCF, 0x11, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};
enum { GUID_MOUSE = 0x6F1D2B60u, GUID_KEYBOARD = 0x6F1D2B61u, GUID_JOYSTICK = 0x6F1D2B70u };

typedef struct {
    u32 kind;
    i32 axis_min, axis_max;
} Device;

static void di_Ok(RcnCtx *ctx) { RET(DI_OK); }
static void di_AddRef(RcnCtx *ctx) { RET(++COM_REFS(A0)); }
static void di_Release(RcnCtx *ctx) {
    u32 refs = --COM_REFS(A0);
    if (refs == 0) rcn_gfree(ctx, PTR(A0));
    RET(refs);
}
static void di_QueryInterface(RcnCtx *ctx) {
    COM_REFS(A0)++;
    wr32(A2, U32(0));
    RET(DI_OK);
}

// GetCapabilities(this, &caps) : DIDEVCAPS { taille, drapeaux, type, axes, boutons, croix, … }.
static void dev_GetCapabilities(RcnCtx *ctx) {
    Device *device = COM_DATA(Device, A0);
    u64 caps = A1;
    u32 size = rd32(caps);
    if (size >= 24 && size <= 64) memset(PTR(caps + 4), 0, size - 4);
    wr32(caps + 4, 1); // DIDC_ATTACHED
    if (device->kind == DEVICE_JOYSTICK) {
        wr32(caps + 8, DIDEVTYPE_JOYSTICK | (2 << 8)); // manette de jeu
        wr32(caps + 12, 2);
        wr32(caps + 16, 8);
        wr32(caps + 20, 1);
    } else if (device->kind == DEVICE_KEYBOARD) {
        wr32(caps + 8, DIDEVTYPE_KEYBOARD);
        wr32(caps + 16, 256);
    } else {
        wr32(caps + 8, DIDEVTYPE_MOUSE);
        wr32(caps + 12, 3);
        wr32(caps + 16, 3);
    }
    RET(DI_OK);
}
// SetProperty(this, property, &header) : seule l'étendue des axes est retenue (DIPROPRANGE : en-tête de
// 16 octets, minimum, maximum).
static void dev_SetProperty(RcnCtx *ctx) {
    Device *device = COM_DATA(Device, A0);
    if (U32(1) == DIPROP_RANGE && A2) {
        device->axis_min = (i32)rd32(A2 + 16);
        device->axis_max = (i32)rd32(A2 + 20);
    }
    RET(DI_OK);
}
// GetDeviceState(this, size, &state)
static void dev_GetDeviceState(RcnCtx *ctx) {
    Device *device = COM_DATA(Device, A0);
    u32 size = U32(1);
    u8 *state = PTR(A2);
    u32 pad = rcn_pad(ctx);
    memset(state, 0, size);
    if (device->kind == DEVICE_KEYBOARD) {
        // Codes de touches DirectInput (DIK_*).
        static const struct {
            u32 button;
            u8 key;
        } keys[] = {
            {RCN_PAD_UP, 0xC8}, {RCN_PAD_DOWN, 0xD0}, {RCN_PAD_LEFT, 0xCB}, {RCN_PAD_RIGHT, 0xCD},
            {RCN_PAD_A, 0x2C},  {RCN_PAD_B, 0x2D},    {RCN_PAD_X, 0x2E},    {RCN_PAD_Y, 0x20},
            {RCN_PAD_START, 0x1C}, {RCN_PAD_SELECT, 0x01},
        };
        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            if ((pad & keys[i].button) && keys[i].key < size) state[keys[i].key] = 0x80;
        }
    } else if (device->kind == DEVICE_JOYSTICK && size >= 80) {
        // DIJOYSTATE : 8 axes, 4 croix, 32 boutons.
        i32 center = device->axis_min + (device->axis_max - device->axis_min) / 2;
        i32 x = (pad & RCN_PAD_LEFT) ? device->axis_min : (pad & RCN_PAD_RIGHT) ? device->axis_max : center;
        i32 y = (pad & RCN_PAD_UP) ? device->axis_min : (pad & RCN_PAD_DOWN) ? device->axis_max : center;
        u64 at = (u64)(uintptr_t)state;
        wr32(at, (u32)x);
        wr32(at + 4, (u32)y);
        for (int i = 2; i < 8; i++) wr32(at + 4 * i, (u32)center);
        for (int i = 0; i < 4; i++) wr32(at + 32 + 4 * i, 0xFFFFFFFFu); // croix au repos
        static const u32 buttons[] = {RCN_PAD_A, RCN_PAD_B, RCN_PAD_X, RCN_PAD_Y, RCN_PAD_L, RCN_PAD_R, RCN_PAD_START, RCN_PAD_SELECT};
        for (int i = 0; i < 8; i++) state[48 + i] = (pad & buttons[i]) ? 0x80 : 0;
    }
    RET(DI_OK);
}
// Description d'un périphérique : DIDEVICEINSTANCEA (580 octets).
static void write_instance(u64 instance, u32 type, u32 guid, const char *name) {
    memset(PTR(instance), 0, 580);
    wr32(instance, 580);
    for (int g = 0; g < 2; g++) { // identifiants de l'exemplaire et du produit
        wr32(instance + 4 + 16 * g, guid);
        memcpy(PTR(instance + 8 + 16 * g), GUID_TAIL, 12);
    }
    wr32(instance + 36, type | (type == DIDEVTYPE_JOYSTICK ? 2 << 8 : 0));
    strcpy(PTR(instance + 40), name);
    strcpy(PTR(instance + 300), name);
}
// GetDeviceInfo(this, &instance)
static void dev_GetDeviceInfo(RcnCtx *ctx) {
    switch (COM_DATA(Device, A0)->kind) {
    case DEVICE_KEYBOARD: write_instance(A1, DIDEVTYPE_KEYBOARD, GUID_KEYBOARD, "Keyboard"); break;
    case DEVICE_MOUSE: write_instance(A1, DIDEVTYPE_MOUSE, GUID_MOUSE, "Mouse"); break;
    default: write_instance(A1, DIDEVTYPE_JOYSTICK, GUID_JOYSTICK, "Gamepad"); break;
    }
    RET(DI_OK);
}
// GetDeviceData(this, item_size, &items, &count, flags) : pas d'événements en attente.
static void dev_GetDeviceData(RcnCtx *ctx) {
    if (A3) wr32(A3, 0);
    RET(DI_OK);
}

#define M(name, fn, args) {#name, fn, args}
static const RcnMethod DEVICE_METHODS[] = {
    M(QueryInterface, di_QueryInterface, 3), M(AddRef, di_AddRef, 1), M(Release, di_Release, 1),
    M(GetCapabilities, dev_GetCapabilities, 2), M(EnumObjects, di_Ok, 4), M(GetProperty, NULL, 3),
    M(SetProperty, dev_SetProperty, 3), M(Acquire, di_Ok, 1), M(Unacquire, di_Ok, 1),
    M(GetDeviceState, dev_GetDeviceState, 3), M(GetDeviceData, dev_GetDeviceData, 5), M(SetDataFormat, di_Ok, 2),
    M(SetEventNotification, di_Ok, 2), M(SetCooperativeLevel, di_Ok, 3), M(GetObjectInfo, NULL, 4),
    M(GetDeviceInfo, dev_GetDeviceInfo, 2), M(RunControlPanel, di_Ok, 3), M(Initialize, di_Ok, 4), M(CreateEffect, NULL, 5),
    M(EnumEffects, di_Ok, 4), M(GetEffectInfo, NULL, 3), M(GetForceFeedbackState, NULL, 2),
    M(SendForceFeedbackCommand, di_Ok, 2), M(EnumCreatedEffectObjects, di_Ok, 4), M(Escape, NULL, 2),
    M(Poll, di_Ok, 1), M(SendDeviceData, NULL, 5), M(EnumEffectsInFile, NULL, 5), M(WriteEffectToFile, NULL, 5),
};

static u32 create_device(RcnCtx *ctx, u64 guid) {
    u32 id = guid ? rd32(guid) : 0;
    u64 object = rcn_com_new(ctx, "IDirectInputDevice7", DEVICE_METHODS, sizeof DEVICE_METHODS / sizeof DEVICE_METHODS[0], sizeof(Device));
    if (!object) return 0;
    Device *device = COM_DATA(Device, object);
    device->kind = id == GUID_KEYBOARD ? DEVICE_KEYBOARD : id == GUID_MOUSE ? DEVICE_MOUSE : DEVICE_JOYSTICK;
    device->axis_min = 0;
    device->axis_max = 65535;
    return (u32)object;
}
// CreateDevice(this, &guid, &device, outer)
static void di_CreateDevice(RcnCtx *ctx) {
    u32 device = create_device(ctx, A1);
    wr32(A2, device);
    RET(device ? DI_OK : DIERR_GENERIC);
}
// CreateDeviceEx(this, &guid, &iid, &device, outer)
static void di_CreateDeviceEx(RcnCtx *ctx) {
    u32 device = create_device(ctx, A1);
    wr32(A3, device);
    RET(device ? DI_OK : DIERR_GENERIC);
}
// EnumDevices(this, type, callback, context, flags) : la fonction du jeu reçoit chaque périphérique
// du type demandé (0 : tous) dans un DIDEVICEINSTANCEA de 580 octets.
static void di_EnumDevices(RcnCtx *ctx) {
    static const struct {
        u32 type, guid;
        const char *name;
    } devices[] = {
        {DIDEVTYPE_MOUSE, GUID_MOUSE, "Mouse"},
        {DIDEVTYPE_KEYBOARD, GUID_KEYBOARD, "Keyboard"},
        {DIDEVTYPE_JOYSTICK, GUID_JOYSTICK, "Gamepad"},
    };
    u32 type = U32(1) & 0xFF, callback = U32(2), context = U32(3);
    u64 instance = (u64)(uintptr_t)rcn_galloc(ctx, 580, 0);
    for (size_t i = 0; instance && i < sizeof devices / sizeof devices[0]; i++) {
        if (type && type != devices[i].type) continue;
        write_instance(instance, devices[i].type, devices[i].guid, devices[i].name);
        u64 args[2] = {instance, context};
        if (rcn_call_guest_n(ctx, callback, 2, args) == 0) break; // DIENUM_STOP
    }
    rcn_gfree(ctx, PTR(instance));
    RET(DI_OK);
}

static const RcnMethod DINPUT_METHODS[] = {
    M(QueryInterface, di_QueryInterface, 3), M(AddRef, di_AddRef, 1), M(Release, di_Release, 1),
    M(CreateDevice, di_CreateDevice, 4), M(EnumDevices, di_EnumDevices, 5), M(GetDeviceStatus, di_Ok, 2),
    M(RunControlPanel, di_Ok, 3), M(Initialize, di_Ok, 3), M(FindDevice, NULL, 4),
    M(CreateDeviceEx, di_CreateDeviceEx, 5),
};

// DirectInputCreateEx(instance, version, iid, &dinput, outer)
static void di_DirectInputCreateEx(RcnCtx *ctx) {
    u64 object = rcn_com_new(ctx, "IDirectInput7", DINPUT_METHODS, sizeof DINPUT_METHODS / sizeof DINPUT_METHODS[0], 4);
    wr32(A3, (u32)object);
    RET(object ? DI_OK : DIERR_GENERIC);
}

const RcnShim rcn_dinput[] = {
    {"DirectInputCreateEx", di_DirectInputCreateEx, S32(5)},
};
const size_t rcn_dinput_count = sizeof rcn_dinput / sizeof rcn_dinput[0];
