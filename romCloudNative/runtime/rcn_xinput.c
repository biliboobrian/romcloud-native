// RomCloud Native : XInput (xinput1_4, xinput1_3, xinput9_1_0) — la manette de l'hôte en manette Xbox n° 0.
#include "rcn_gui.h"
#include "rcn_internal.h"

enum {
    ERROR_SUCCESS = 0,
    ERROR_DEVICE_NOT_CONNECTED = 1167,
    ERROR_EMPTY = 4306,
    XI_DPAD_UP = 0x1, XI_DPAD_DOWN = 0x2, XI_DPAD_LEFT = 0x4, XI_DPAD_RIGHT = 0x8, XI_START = 0x10, XI_BACK = 0x20,
    XI_LTHUMB = 0x40, XI_RTHUMB = 0x80, XI_LB = 0x100, XI_RB = 0x200, XI_A = 0x1000, XI_B = 0x2000, XI_X = 0x4000,
    XI_Y = 0x8000,
};

static int has_pad(RcnCtx *ctx) { return ctx->proc->host->pad != NULL; }

static volatile u32 packet;

// XINPUT_GAMEPAD (12 octets) : boutons, gâchettes 0-255, sticks -32768-32767 (y vers le haut).
static void read_gamepad(RcnCtx *ctx, u8 *g) {
    u32 b = rcn_pad_buttons(ctx);
    i16 axes[6];
    rcn_pad_axes(ctx, axes);
    u16 w = 0;
    static const struct {
        u32 host;
        u16 xi;
    } map[] = {{RCN_PAD_UP, XI_DPAD_UP}, {RCN_PAD_DOWN, XI_DPAD_DOWN}, {RCN_PAD_LEFT, XI_DPAD_LEFT}, {RCN_PAD_RIGHT, XI_DPAD_RIGHT},
               {RCN_PAD_START, XI_START}, {RCN_PAD_SELECT, XI_BACK}, {RCN_PAD_L3, XI_LTHUMB}, {RCN_PAD_R3, XI_RTHUMB},
               {RCN_PAD_L, XI_LB}, {RCN_PAD_R, XI_RB}, {RCN_PAD_A, XI_A}, {RCN_PAD_B, XI_B}, {RCN_PAD_X, XI_X}, {RCN_PAD_Y, XI_Y}};
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (b & map[i].host) w |= map[i].xi;
    }
    // Gâchettes : axes analogiques, sinon boutons tout-ou-rien.
    u8 lt = (u8)(axes[4] > 0 ? axes[4] >> 7 : 0), rt = (u8)(axes[5] > 0 ? axes[5] >> 7 : 0);
    if (b & RCN_PAD_LT) lt = 255;
    if (b & RCN_PAD_RT) rt = 255;
    i16 ly = axes[1] == -32768 ? 32767 : (i16)-axes[1], ry = axes[3] == -32768 ? 32767 : (i16)-axes[3];
    memcpy(g, &w, 2);
    g[2] = lt;
    g[3] = rt;
    memcpy(g + 4, &axes[0], 2);
    memcpy(g + 6, &ly, 2);
    memcpy(g + 8, &axes[2], 2);
    memcpy(g + 10, &ry, 2);
}

// XInputGetState(index, XINPUT_STATE *) : numéro de paquet puis XINPUT_GAMEPAD.
static void xi_log(const char *what, u32 index) {
    static int told[8];
    static const char *const names[8] = {"GetState", "GetCapabilities", "SetState", "GetBatteryInformation", "Enable", "GetKeystroke", "autre", "autre"};
    for (int i = 0; i < 8; i++) {
        if (names[i] == what || !strcmp(names[i], what)) {
            if (told[i]++ < 2) rcn_log("xinput : %s(%u)", what, index);
            return;
        }
    }
}
static void xi_GetState(RcnCtx *ctx) {
    u32 index = (u32)A0;
    xi_log("GetState", index);
    if (index != 0 || !has_pad(ctx) || !A1) {
        RET(ERROR_DEVICE_NOT_CONNECTED);
        return;
    }
    u8 g[12];
    read_gamepad(ctx, g);
    static u8 last[12];
    if (memcmp(g, last, 12)) {
        memcpy(last, g, 12);
        packet++;
    }
    wr32(A1, packet);
    memcpy(PTR(A1 + 4), g, 12);
    RET(ERROR_SUCCESS);
}
static void xi_SetState(RcnCtx *ctx) { RET((u32)A0 == 0 && has_pad(ctx) ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED); }
// XInputGetCapabilities(index, flags, XINPUT_CAPABILITIES *) : manette Xbox complète.
static void xi_GetCapabilities(RcnCtx *ctx) {
    xi_log("GetCapabilities", (u32)A0);
    if ((u32)A0 != 0 || !has_pad(ctx) || !A2) {
        RET(ERROR_DEVICE_NOT_CONNECTED);
        return;
    }
    u8 *c = PTR(A2);
    memset(c, 0, 20);
    c[0] = 1;    // XINPUT_DEVTYPE_GAMEPAD
    c[1] = 1;    // XINPUT_DEVSUBTYPE_GAMEPAD
    u16 all = 0xF3FF, flags = 0;
    memcpy(c + 2, &flags, 2);
    memcpy(c + 4, &all, 2); // boutons
    c[6] = c[7] = 255;      // gâchettes
    i16 full = 32767;
    for (int i = 0; i < 4; i++) memcpy(c + 8 + 2 * i, &full, 2);
    u16 motor = 65535;
    memcpy(c + 16, &motor, 2);
    memcpy(c + 18, &motor, 2);
    RET(ERROR_SUCCESS);
}
static void xi_Enable(RcnCtx *ctx) { (void)ctx; }
// XInputGetBatteryInformation(index, type, XINPUT_BATTERY_INFORMATION *) : filaire.
static void xi_GetBatteryInformation(RcnCtx *ctx) {
    if ((u32)A0 != 0 || !has_pad(ctx)) {
        RET(ERROR_DEVICE_NOT_CONNECTED);
        return;
    }
    if (A2) {
        wr8(A2, 1);     // BATTERY_TYPE_WIRED
        wr8(A2 + 1, 3); // BATTERY_LEVEL_FULL
    }
    RET(ERROR_SUCCESS);
}
static void xi_GetKeystroke(RcnCtx *ctx) { RET((u32)A0 == 0 && has_pad(ctx) ? ERROR_EMPTY : ERROR_DEVICE_NOT_CONNECTED); }
static void xi_GetAudioDeviceIds(RcnCtx *ctx) { RET(ERROR_DEVICE_NOT_CONNECTED); }

const RcnShim rcn_xinput[] = {
    {"XInputGetState", xi_GetState, 0},
    {"#100", xi_GetState, 0}, // XInputGetStateEx (bouton guide en plus)
    {"XInputSetState", xi_SetState, 0},
    {"XInputGetCapabilities", xi_GetCapabilities, 0},
    {"XInputEnable", xi_Enable, 0},
    {"XInputGetBatteryInformation", xi_GetBatteryInformation, 0},
    {"XInputGetKeystroke", xi_GetKeystroke, 0},
    {"XInputGetAudioDeviceIds", xi_GetAudioDeviceIds, 0},
    {"XInputGetDSoundAudioDeviceGuids", xi_GetAudioDeviceIds, 0},
};
const size_t rcn_xinput_count = sizeof rcn_xinput / sizeof rcn_xinput[0];
