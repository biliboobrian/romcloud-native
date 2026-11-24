// RomCloud Native : petites bibliothèques système des jeux 64 bits récents (Unity…). Le jeu tourne seul,
// hors ligne : registre sans effet durable, pas de certificats, pas de périphériques HID ni de réseau,
// pas d'OpenGL par WGL (le rendu passe par Direct3D), dossiers de l'utilisateur sous « C:\Users\Player ».
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "rcn_internal.h"

static char *utf8(u64 text) { return text ? rcn_utf16_to_utf8(PTR(text), -1) : NULL; }
static u32 put_wide(u64 buffer, u32 size, const char *text) {
    size_t len = rcn_utf8_to_utf16(text, strlen(text), NULL);
    if (!buffer || size <= len) return (u32)len + 1;
    rcn_utf8_to_utf16(text, strlen(text), PTR(buffer));
    wr16(buffer + 2 * len, 0);
    return (u32)len;
}

static void s_Zero(RcnCtx *ctx) { RET(0); }
static void s_True(RcnCtx *ctx) { RET(1); }
static void s_MinusOne(RcnCtx *ctx) { RET_I32(-1); }
static void s_InvalidHandle(RcnCtx *ctx) { RET(~0ull); }
static void s_NotImpl(RcnCtx *ctx) { RET(0x80004001u); }  // E_NOTIMPL
static void s_Fail(RcnCtx *ctx) { RET(0x80004005u); }     // E_FAIL
static void s_NotFound(RcnCtx *ctx) { RET(2); }           // ERROR_FILE_NOT_FOUND
static void s_NotSupported(RcnCtx *ctx) { RET(50); }      // ERROR_NOT_SUPPORTED
static void s_NoMoreItems(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 259;
    RET(0);
}

// --- advapi32 ---

static void a_CryptAcquireContextW(RcnCtx *ctx) {
    if (A0) wrp(ctx, A0, 0x7101); // HCRYPTPROV : ULONG_PTR
    RET(1);
}
static void a_CryptGenRandom(RcnCtx *ctx) { // (provider, length, buffer)
    for (u32 i = 0; i < (u32)A1; i++) wr8(A2 + i, (u8)(rand() >> 7));
    RET(1);
}
static void a_GetUserName(RcnCtx *ctx, int wide) { // (buffer, &size)
    u32 size = rd32(A1);
    static const char name[] = "Player";
    if (size < sizeof name) {
        wr32(A1, sizeof name);
        LAST_ERROR(ctx) = 122;
        RET(0);
        return;
    }
    if (wide) put_wide(A0, size, name);
    else strcpy(PTR(A0), name);
    wr32(A1, sizeof name);
    RET(1);
}
static void a_GetUserNameA(RcnCtx *ctx) { a_GetUserName(ctx, 0); }
static void a_GetUserNameW(RcnCtx *ctx) { a_GetUserName(ctx, 1); }
// RegCreateKeyExW(key, subkey, reserved, class, options, access, security, &result, &disposition)
static void a_RegCreateKeyExW(RcnCtx *ctx) {
    if (STK(7)) wrp(ctx, STK(7), 0x7201);
    if (STK(8)) wr32(STK(8), 1); // REG_CREATED_NEW_KEY
    RET(0);
}
static void a_RegCreateKeyW(RcnCtx *ctx) {
    if (A2) wrp(ctx, A2, 0x7201);
    RET(0);
}

static const RcnShim advapi32[] = {
    {"CryptAcquireContextW", a_CryptAcquireContextW, S32(5)}, {"CryptCreateHash", s_Zero, S32(5)},
    {"CryptDestroyHash", s_True, S32(1)},                     {"CryptGenRandom", a_CryptGenRandom, S32(3)},
    {"CryptGetHashParam", s_Zero, S32(5)},                    {"CryptHashData", s_Zero, S32(4)},
    {"CryptReleaseContext", s_True, S32(2)},                  {"GetSidSubAuthority", s_Zero, 0},
    {"GetTokenInformation", s_Zero, S32(5)},                  {"GetUserNameA", a_GetUserNameA, S32(2)},
    {"GetUserNameW", a_GetUserNameW, S32(2)},                 {"RegCreateKeyExW", a_RegCreateKeyExW, S32(9)},
    {"RegCreateKeyW", a_RegCreateKeyW, S32(3)},               {"RegDeleteValueA", s_NotFound, S32(2)},
    {"RegSetValueExA", s_Zero, S32(6)},                       {"RegSetValueExW", s_Zero, S32(6)},
    {NULL, NULL, 0},
};

// --- crypt32, dbghelp, dwmapi, gdi32, opengl32 ---

static const RcnShim crypt32[] = {
    {"CertAddEncodedCertificateToStore", s_Zero, 0}, {"CertCloseStore", s_True, 0},
    {"CertFreeCertificateChain", s_Zero, 0},         {"CertFreeCertificateContext", s_True, 0},
    {"CertGetCertificateChain", s_Zero, 0},          {"CertOpenStore", s_Zero, 0},
    {"CertVerifyCertificateChainPolicy", s_Zero, 0}, {NULL, NULL, 0},
};
// ImageNtHeader(image) : en-tête PE (« PE\0\0 ») de l'image d'un module.
static void d_ImageNtHeader(RcnCtx *ctx) { RET(A0 && rd16(A0) == 0x5A4D ? A0 + rd32(A0 + 0x3C) : 0); }
static const RcnShim dbghelp[] = {{"SymFromAddr", s_Zero, 0}, {"SymInitialize", s_Zero, 0},
                                  {"ImageNtHeader", d_ImageNtHeader, 0}, {"MiniDumpWriteDump", s_Zero, 0}, {NULL, NULL, 0}};
static const RcnShim dwmapi[] = {{"DwmGetWindowAttribute", s_Fail, 0}, {"DwmSetWindowAttribute", s_Zero, 0}, {NULL, NULL, 0}};
// GetDeviceCaps(dc, index) : l'écran que décrivent GetSystemMetrics et EnumDisplaySettings (1920 × 1080,
// 32 bits, 60 Hz), à 96 points par pouce. Unreal Engine sans shcore.dll tire son échelle d'interface de
// LOGPIXELSX / 96 : à 0, interface de taille nulle, canevas infini et caméra NaN (Clair Obscur, écran noir).
static void s_GetDeviceCaps(RcnCtx *ctx) {
    switch ((u32)A1) {
    case 4: RET(527); break;   // HORZSIZE (mm, écran de 24 pouces)
    case 6: RET(296); break;   // VERTSIZE
    case 8: RET(rcn_screen_size(0)); break;  // HORZRES
    case 10: RET(rcn_screen_size(1)); break; // VERTRES
    case 12: RET(32); break;   // BITSPIXEL
    case 14: RET(1); break;    // PLANES
    case 24: RET_I32(-1); break; // NUMCOLORS (plus de 256 couleurs)
    case 88:                   // LOGPIXELSX
    case 90: RET(96); break;   // LOGPIXELSY
    case 116: RET(60); break;  // VREFRESH
    case 117: RET(rcn_screen_size(1)); break; // DESKTOPVERTRES
    case 118: RET(rcn_screen_size(0)); break; // DESKTOPHORZRES
    default: RET(0); break;
    }
}
static const RcnShim gdi32[] = {
    {"ChoosePixelFormat", s_Zero, S32(2)}, {"SetPixelFormat", s_Zero, S32(3)}, {"SwapBuffers", s_Zero, S32(1)},
    {"DescribePixelFormat", s_Zero, 0}, {"GetDeviceCaps", s_GetDeviceCaps, 0}, {NULL, NULL, 0},
};
static const RcnShim opengl32[] = {
    {"wglCreateContext", s_Zero, S32(1)},     {"wglDeleteContext", s_True, S32(1)}, {"wglGetCurrentContext", s_Zero, S32(0)},
    {"wglGetCurrentDC", s_Zero, S32(0)},      {"wglGetProcAddress", s_Zero, S32(1)}, {"wglMakeCurrent", s_Zero, S32(2)},
    {NULL, NULL, 0},
};

// --- hid, setupapi : aucun périphérique ---

static void h_GetHidGuid(RcnCtx *ctx) { memset(PTR(A0), 0, 16); }
static const RcnShim hid[] = {
    {"HidD_FreePreparsedData", s_True, S32(1)},   {"HidD_GetAttributes", s_Zero, 0},
    {"HidD_GetHidGuid", h_GetHidGuid, S32(1)},    {"HidD_GetManufacturerString", s_Zero, 0},
    {"HidD_GetPreparsedData", s_Zero, S32(2)},    {"HidD_GetProductString", s_Zero, S32(3)},
    {"HidD_GetSerialNumberString", s_Zero, 0}, {"HidP_GetButtonCaps", s_Zero, S32(4)},
    {"HidP_GetCaps", s_Zero, S32(2)},             {"HidP_GetData", s_Zero, S32(6)},
    {"HidP_GetValueCaps", s_Zero, S32(4)},        {"HidP_MaxDataListLength", s_Zero, S32(2)},
    {"HidP_SetUsages", s_Zero, 0},           {"HidP_SetUsageValue", s_Zero, 0},
    {NULL, NULL, 0},
};
static const RcnShim setupapi[] = {
    {"SetupDiDestroyDeviceInfoList", s_True, 0}, {"SetupDiEnumDeviceInfo", s_NoMoreItems, 0},
    {"SetupDiEnumDeviceInterfaces", s_NoMoreItems, 0}, {"SetupDiGetClassDevsA", s_InvalidHandle, 0},
    {"SetupDiGetClassDevsW", s_InvalidHandle, 0}, {"SetupDiGetDeviceInterfaceDetailW", s_Zero, 0},
    {NULL, NULL, 0},
};

// --- imm32 : pas de méthode de saisie ---

static const RcnShim imm32[] = {
    {"ImmAssociateContext", s_Zero, S32(2)}, {"ImmAssociateContextEx", s_True, S32(3)}, {"ImmGetCompositionStringW", s_Zero, S32(4)},
    {"ImmGetContext", s_Zero, S32(1)},       {"ImmGetConversionStatus", s_Zero, S32(3)}, {"ImmNotifyIME", s_Zero, 0},
    {"ImmReleaseContext", s_True, S32(2)},   {"ImmSetCompositionStringW", s_Zero, S32(6)}, {NULL, NULL, 0},
};

// --- iphlpapi, winhttp, ws2_32 (compléments) : pas de réseau ---

static void n_GetAdaptersAddresses(RcnCtx *ctx) { RET(232); } // ERROR_NO_DATA
static const RcnShim iphlpapi[] = {
    {"GetAdaptersAddresses", n_GetAdaptersAddresses, 0}, {"GetIfEntry", s_NotSupported, 0},
    {"GetNetworkParams", s_NotSupported, 0}, {NULL, NULL, 0},
};
static void n_WinHttpOpen(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 12001;
    RET(0);
}
static const RcnShim winhttp[] = {
    {"WinHttpCloseHandle", s_True, 0}, {"WinHttpGetIEProxyConfigForCurrentUser", s_Zero, S32(1)},
    {"WinHttpGetProxyForUrl", s_Zero, 0}, {"WinHttpOpen", n_WinHttpOpen, 0}, {NULL, NULL, 0},
};
// inet_addr(text) : adresse en ordre réseau, ou INADDR_NONE.
static void n_inet_addr(RcnCtx *ctx) {
    unsigned a, b, c, d;
    if (!A0 || sscanf(STR(A0), "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        RET(0xFFFFFFFFu);
        return;
    }
    RET(a | b << 8 | c << 16 | d << 24);
}
static void n_inet_ntoa(RcnCtx *ctx) {
    static char text[16];
    u32 v = (u32)A0;
    snprintf(text, sizeof text, "%u.%u.%u.%u", v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24);
    RET((u64)(uintptr_t)text);
}
static void n_gethostname(RcnCtx *ctx) {
    if ((i32)A1 < 9) {
        RET_I32(-1);
        return;
    }
    strcpy(PTR(A0), "romcloud");
    RET(0);
}
static void n_getnameinfo(RcnCtx *ctx) { RET(11001); }
// Événements de Winsock : événements de kernel32 à réarmement manuel (libcurl en crée un pour se réveiller).
static void n_WSACreateEvent(RcnCtx *ctx) { RET(rcn_event_new(ctx, 1, 0)); }
static void n_WSASetEvent(RcnCtx *ctx) {
    rcn_event_signal(ctx->proc, A0);
    RET(1);
}
static void n_WSAResetEvent(RcnCtx *ctx) { rcn_wsa_reset_event(ctx); }
static void n_WSACloseEvent(RcnCtx *ctx) { rcn_wsa_close_event(ctx); }
// WSAWaitForMultipleEvents(nombre, événements, tous, délai, alertable) : mêmes valeurs que WaitForMultipleObjects.
static void n_WSAWaitForMultipleEvents(RcnCtx *ctx) {
    u32 count = (u32)A0;
    if (!count || count > 64 || !A1) {
        RET(0xFFFFFFFFu);
        return;
    }
    u64 handles[64];
    for (u32 i = 0; i < count; i++) handles[i] = rdp(ctx, A1 + PTR_SIZE(ctx) * (u64)i);
    RET(rcn_wait_handles(ctx, count, handles, (u32)A2 != 0, (u32)A3));
}
// Sockets, résolution, ConnectEx : rcn_mm_x64.c (réseau d'un PC hors ligne).
static const RcnShim ws2[] = {
    {"#11", n_inet_addr, 0},  {"#12", n_inet_ntoa, 0},   {"#51", s_Zero, 0},   {"#52", s_Zero, 0},
    {"#53", s_Zero, 0},       {"#57", n_gethostname, 0}, {"#103", s_Zero, 0},  {"#108", s_Zero, 0},
    {"#151", s_Zero, 0},      {"getnameinfo", n_getnameinfo, S32(7)},
    {"WSACloseEvent", n_WSACloseEvent, S32(1)}, {"WSACreateEvent", n_WSACreateEvent, S32(0)},
    {"WSAEnumNetworkEvents", s_MinusOne, S32(3)}, {"WSAEventSelect", s_MinusOne, S32(3)}, {"WSAPoll", s_MinusOne, 0},
    {"WSAResetEvent", n_WSAResetEvent, S32(1)}, {"WSASetEvent", n_WSASetEvent, S32(1)},
    {"WSASendDisconnect", s_MinusOne, 0},
    {"WSAWaitForMultipleEvents", n_WSAWaitForMultipleEvents, S32(5)}, {NULL, NULL, 0},
};

// --- ole32 / oleaut32 (compléments) ---

static void o_PropVariantCopy(RcnCtx *ctx) {
    memcpy(PTR(A0), PTR(A1), 24);
    RET(0);
}
// StringFromGUID2(&guid, buffer, size) : « {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} ».
static void o_StringFromGUID2(RcnCtx *ctx) {
    u64 g = A0;
    char text[40];
    snprintf(text, sizeof text, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", rd32(g), rd16(g + 4), rd16(g + 6), rd8(g + 8),
             rd8(g + 9), rd8(g + 10), rd8(g + 11), rd8(g + 12), rd8(g + 13), rd8(g + 14), rd8(g + 15));
    if ((u32)A2 < 39) {
        RET(0);
        return;
    }
    put_wide(A1, (u32)A2, text);
    RET(39);
}
static const RcnShim ole32[] = {
    {"CoCreateFreeThreadedMarshaler", s_NotImpl, S32(2)}, {"CoGetContextToken", s_NotImpl, 0},
    {"CoInitialize", s_Zero, S32(1)},                     {"CoSetProxyBlanket", s_Zero, S32(8)},
    {"PropVariantCopy", o_PropVariantCopy, 0},       {"StringFromGUID2", o_StringFromGUID2, S32(3)},
    {NULL, NULL, 0},
};
// BSTR : longueur en octets (32 bits) puis texte UTF-16 et zéro ; l'adresse rendue est celle du texte.
static u64 bstr(RcnCtx *ctx, u64 text, u32 chars) {
    u8 *block = rcn_galloc(ctx, 4 + 2 * (size_t)chars + 2, 1);
    if (!block) return 0;
    wr32((u64)(uintptr_t)block, 2 * chars);
    if (text) memcpy(block + 4, PTR(text), 2 * (size_t)chars);
    return (u64)(uintptr_t)block + 4;
}
static void o_SysAllocString(RcnCtx *ctx) {
    u32 n = 0;
    while (A0 && rd16(A0 + 2 * (u64)n)) n++;
    RET(A0 ? bstr(ctx, A0, n) : 0);
}
static void o_SysAllocStringLen(RcnCtx *ctx) { RET(bstr(ctx, A0, (u32)A1)); }
static void o_SysFreeString(RcnCtx *ctx) {
    if (A0) rcn_gfree(ctx, PTR(A0 - 4));
}
static void o_SysStringLen(RcnCtx *ctx) { RET(A0 ? rd32(A0 - 4) / 2 : 0); }
static const RcnShim oleaut32[] = {
    {"#2", o_SysAllocString, S32(1)}, {"#4", o_SysAllocStringLen, S32(2)}, {"#6", o_SysFreeString, S32(1)},
    {"#7", o_SysStringLen, S32(1)},   {"#12", s_NotImpl, S32(4)},     {"#24", s_NotImpl, 0},
    {"#77", s_NotImpl, 0},       {"SysAllocString", o_SysAllocString, 0},
    {"SysAllocStringLen", o_SysAllocStringLen, 0}, {"SysStringLen", o_SysStringLen, 0},
    {NULL, NULL, 0},
};

// --- shell32, shlwapi, version ---

// CommandLineToArgvW(line, &count) : bloc unique (tableau de pointeurs puis textes), libéré par LocalFree.
static void sh_CommandLineToArgvW(RcnCtx *ctx) {
    u64 line = A0;
    u32 len = 0;
    while (line && rd16(line + 2 * (u64)len)) len++;
    u32 max_args = len / 2 + 2;
    u32 psz = PTR_SIZE(ctx);
    u8 *block = rcn_galloc(ctx, psz * (size_t)max_args + 2 * ((size_t)len + max_args), 1);
    if (!block) {
        RET(0);
        return;
    }
    u64 argv = (u64)(uintptr_t)block, out = argv + psz * (u64)max_args;
    u32 argc = 0, i = 0;
    while (i < len) {
        while (i < len && (rd16(line + 2 * (u64)i) == ' ' || rd16(line + 2 * (u64)i) == '\t')) i++;
        if (i >= len) break;
        wrp(ctx, argv + psz * (u64)argc++, out);
        int quoted = 0;
        while (i < len) {
            u16 c = rd16(line + 2 * (u64)i);
            if (c == '"') {
                quoted = !quoted;
                i++;
                continue;
            }
            if (!quoted && (c == ' ' || c == '\t')) break;
            wr16(out, c);
            out += 2;
            i++;
        }
        wr16(out, 0);
        out += 2;
    }
    if (A1) wr32(A1, argc);
    RET(argv);
}
static void sh_ShellExecuteW(RcnCtx *ctx) { RET(2); } // ≤ 32 : échec
static const char *folder_path(u32 csidl) {
    switch (csidl & 0xFF) {
    case 0x1A: return "C:\\Users\\Player\\AppData\\Roaming"; // CSIDL_APPDATA
    case 0x1C: return "C:\\Users\\Player\\AppData\\Local";   // CSIDL_LOCAL_APPDATA
    case 0x05: return "C:\\Users\\Player\\Documents";        // CSIDL_PERSONAL
    case 0x23: return "C:\\ProgramData";                     // CSIDL_COMMON_APPDATA
    case 0x28: return "C:\\Users\\Player";                   // CSIDL_PROFILE
    default: return "C:\\Users\\Player";
    }
}
// SHGetFolderPathW / A(window, csidl, token, flags, path)
static void sh_SHGetFolderPathW(RcnCtx *ctx) {
    put_wide(STK(4), 260, folder_path((u32)A1));
    RET(0);
}
static void sh_SHGetFolderPathA(RcnCtx *ctx) {
    const char *path = folder_path((u32)A1);
    memcpy(PTR(STK(4)), path, strlen(path) + 1);
    RET(0);
}
// SHGetSpecialFolderPathW / A(fenêtre, chemin, csidl, créer) : mêmes dossiers que SHGetFolderPath.
static void sh_SHGetSpecialFolderPathW(RcnCtx *ctx) {
    if (A1) put_wide(A1, 260, folder_path((u32)A2));
    RET(1);
}
static void sh_SHGetSpecialFolderPathA(RcnCtx *ctx) {
    const char *path = folder_path((u32)A2);
    if (A1) memcpy(PTR(A1), path, strlen(path) + 1);
    RET(1);
}
static const RcnShim shell32[] = {
    {"CommandLineToArgvW", sh_CommandLineToArgvW, S32(2)}, {"ShellExecuteW", sh_ShellExecuteW, S32(6)},
    {"SHFileOperationW", s_True, S32(1)},                  {"SHGetFolderPathW", sh_SHGetFolderPathW, S32(5)},
    {NULL, NULL, 0},
};
// PathCanonicalizeW(destination, source) : copie, « \.\ » retirés.
static void sh_PathCanonicalizeW(RcnCtx *ctx) {
    char *s = utf8(A1);
    if (!s) {
        RET(0);
        return;
    }
    char *p;
    while ((p = strstr(s, "\\.\\"))) memmove(p, p + 2, strlen(p + 2) + 1);
    put_wide(A0, 260, s);
    free(s);
    RET(1);
}
// PathFileExistsW(chemin) : le fichier ou le dossier existe-t-il dans le dossier du jeu.
static void sh_PathFileExistsW(RcnCtx *ctx) {
    char *name = A0 ? utf8(A0) : NULL;
    char *path = name ? rcn_host_path(ctx, name) : NULL;
    struct stat st;
    int exists = path && stat(path, &st) == 0;
    free(path);
    free(name);
    RET(exists ? 1 : 0);
}
static const RcnShim shlwapi[] = {
    {"PathCanonicalizeW", sh_PathCanonicalizeW, S32(2)}, {"PathFileExistsW", sh_PathFileExistsW, S32(1)},
    {"SHDeleteKeyW", s_NotFound, S32(2)}, {NULL, NULL, 0},
};
static const RcnShim version[] = {
    {"GetFileVersionInfoA", s_Zero, S32(4)}, {"GetFileVersionInfoSizeA", s_Zero, S32(2)}, {"VerQueryValueA", s_Zero, S32(4)},
    {"GetFileVersionInfoW", s_Zero, S32(4)}, {"GetFileVersionInfoSizeW", s_Zero, S32(2)}, {"VerQueryValueW", s_Zero, S32(4)},
    {NULL, NULL, 0},
};

// --- winmm (variantes A) ---

static void m_waveOutGetDevCapsA(RcnCtx *ctx) { // (device, &caps, size)
    u64 caps = A1;
    memset(PTR(caps), 0, (u32)A2 < 52 ? (u32)A2 : 52);
    strcpy(PTR(caps + 8), "RomCloud");
    wr32(caps + 40, 0xFFFFF);
    wr16(caps + 44, 2);
    RET(0);
}
static void m_NoDriver(RcnCtx *ctx) { RET(6); }
static const RcnShim winmm[] = {{"waveInGetDevCapsA", m_NoDriver, S32(3)}, {"waveOutGetDevCapsA", m_waveOutGetDevCapsA, S32(3)}, {NULL, NULL, 0}};

// --- Recherche ---

static int starts_with(const char *text, const char *prefix) {
    for (; *prefix; text++, prefix++) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != *prefix) return 0;
    }
    return 1;
}

// --- Services, certificats, réseau, Uniscribe : absents (PC hors ligne) ---

static void s_AccessDenied(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 5;
    RET(0);
}
static void s_EventSource(RcnCtx *ctx) { RET(0x7201); }
static void s_BcryptNotSupported(RcnCtx *ctx) { RET(0xC00000BBu); } // STATUS_NOT_SUPPORTED
// InternetGetConnectedState(&options, réservé) : pas de connexion.
static void s_InternetGetConnectedState(RcnCtx *ctx) {
    if (A0) wr32(A0, 0);
    RET(0);
}
static void s_WsaInvalid(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 10022; // WSAEINVAL
    RET_I32(-1);
}
static void s_Nothing(RcnCtx *ctx) { (void)ctx; }
// UuidCreate(&uuid) : aléatoire (version 4).
static void s_UuidCreate(RcnCtx *ctx) {
    for (u32 i = 0; i < 16 && A0; i++) wr8(A0 + i, (u8)(rand() >> 7));
    if (A0) wr8(A0 + 7, (u8)((rd8(A0 + 7) & 0x0F) | 0x40));
    RET(0);
}
static const RcnShim advapi32_more[] = {
    {"CryptAcquireContextA", a_CryptAcquireContextW, S32(5)},
    {"OpenSCManagerA", s_AccessDenied, 0}, {"OpenServiceA", s_AccessDenied, 0}, {"CloseServiceHandle", s_True, 0},
    {"ControlService", s_AccessDenied, 0}, {"QueryServiceStatus", s_AccessDenied, 0}, {"StartServiceA", s_AccessDenied, 0},
    {"RegisterEventSourceW", s_EventSource, 0}, {"ReportEventW", s_True, 0}, {"DeregisterEventSource", s_True, S32(1)},
    {NULL, NULL, 0},
};
static const RcnShim crypt32_more[] = {
    {"CertAddCertificateContextToStore", s_Zero, 0}, {"CertCreateCertificateChainEngine", s_Zero, 0},
    {"CertFindCertificateInStore", s_Zero, 0}, {"CertFreeCertificateChainEngine", s_Nothing, 0},
    {"CertGetNameStringA", s_Zero, 0}, {"CryptQueryObject", s_Zero, 0}, {"CryptStringToBinaryA", s_Zero, 0},
    {NULL, NULL, 0},
};
static const RcnShim gdi32_more[] = {
    {"CreateCompatibleBitmap", s_Zero, S32(3)}, {"DeleteObject", s_True, 0}, {"GetDIBits", s_Zero, S32(7)}, {"GetObjectW", s_Zero, S32(3)},
    {"AddFontMemResourceEx", s_Zero, 0}, {"CreateFontIndirectW", s_Zero, S32(1)}, {"RemoveFontMemResourceEx", s_True, 0},
    {"SelectObject", s_Zero, 0},
    {NULL, NULL, 0},
};
// --- Compléments : variantes A, et fonctions sans objet (réseau, crochets, DirectInput) ---

// timeGetDevCaps(&caps, taille) : période de 1 ms à 1000 s.
static void m_timeGetDevCaps(RcnCtx *ctx) {
    if (!A0 || (u32)A1 < 8) {
        RET(97); // TIMERR_NOCANDO
        return;
    }
    wr32(A0, 1), wr32(A0 + 4, 1000000);
    RET(0);
}
// --- powrprof ---
// CallNtPowerInformation(niveau, entrée, taille, sortie, taille) : ProcessorInformation (11) — un
// PROCESSOR_POWER_INFORMATION de 24 octets par processeur (fréquences de 2 GHz) ; autres niveaux : sortie
// à zéro. STATUS_SUCCESS, ou STATUS_BUFFER_TOO_SMALL (0xC0000023).
static void pw_CallNtPowerInformation(RcnCtx *ctx) {
    u32 level = (u32)A0, size = (u32)STK(4);
    u64 out = A3;
    if (out && size) memset(PTR(out), 0, size);
    if (level == 11) {
        u32 cpus = rcn_cpu_count();
        if (!out || size < 24 * cpus) {
            RET(0xC0000023u);
            return;
        }
        for (u32 i = 0; i < cpus; i++) {
            u64 p = out + 24 * (u64)i;
            wr32(p, i);          // Number
            wr32(p + 4, 2000);   // MaxMhz
            wr32(p + 8, 2000);   // CurrentMhz
            wr32(p + 12, 2000);  // MhzLimit
        }
    }
    RET(0);
}
static const RcnShim powrprof[] = {
    {"CallNtPowerInformation", pw_CallNtPowerInformation, S32(5)},
    {NULL, NULL, 0},
};

static const RcnShim more_sys[] = {
    {"GetStockObject", s_True, 0},
    {"RegCreateKeyExA", a_RegCreateKeyExW, S32(9)}, {"RegGetValueA", s_NotFound, 0}, {"RegGetValueW", s_NotFound, 0},
    {"SHGetFolderPathA", sh_SHGetFolderPathA, 0}, {"ShellExecuteA", sh_ShellExecuteW, 0},
    {"SHGetSpecialFolderPathA", sh_SHGetSpecialFolderPathA, 0},
    {"SHGetSpecialFolderPathW", sh_SHGetSpecialFolderPathW, 0},
    {"HidD_GetFeature", s_Zero, 0}, {"HidD_SetFeature", s_Zero, 0},
    {"WinHttpAddRequestHeaders", s_Zero, 0}, {"WinHttpConnect", s_Zero, 0}, {"WinHttpOpenRequest", s_Zero, 0},
    {"WinHttpQueryDataAvailable", s_Zero, 0}, {"WinHttpQueryHeaders", s_Zero, 0}, {"WinHttpReadData", s_Zero, 0},
    {"WinHttpReceiveResponse", s_Zero, 0}, {"WinHttpSendRequest", s_Zero, 0}, {"WinHttpSetOption", s_Zero, 0},
    {"WinHttpSetStatusCallback", s_InvalidHandle, 0}, {"WinHttpSetTimeouts", s_Zero, 0}, {"WinHttpWriteData", s_Zero, 0},
    {"timeGetDevCaps", m_timeGetDevCaps, 0}, {"waveOutPause", s_Zero, 0}, {"waveOutRestart", s_Zero, 0},
    {"waveOutSetVolume", s_Zero, 0},
    {"#101", s_MinusOne, 0}, // WSAAsyncSelect
    {"GetAddrInfoW", n_getnameinfo, 0}, {"FreeAddrInfoW", s_Nothing, 0},
    {"CreateStreamOnHGlobal", s_NotImpl, 0},
    {NULL, NULL, 0},
};
static const RcnShim iphlpapi_more[] = {{"IcmpCreateFile", s_InvalidHandle, 0}, {"IcmpSendEcho", s_Zero, 0}, {NULL, NULL, 0}};
static const RcnShim wininet[] = {{"InternetGetConnectedState", s_InternetGetConnectedState, 0}, {NULL, NULL, 0}};
static const RcnShim ws2_more[] = {{NULL, NULL, 0}}; // WSAAddressToStringW… : rcn_mm_x64.c
static const RcnShim mswsock[] = {{"AcceptEx", s_Zero, 0}, {"GetAcceptExSockaddrs", s_Nothing, 0}, {NULL, NULL, 0}};
static const RcnShim bcrypt_more[] = {
    {"BCryptCloseAlgorithmProvider", s_Zero, 0}, {"BCryptCreateHash", s_BcryptNotSupported, 0},
    {"BCryptDecrypt", s_BcryptNotSupported, 0}, {"BCryptDeriveKey", s_BcryptNotSupported, 0},
    {"BCryptDestroyHash", s_Zero, 0}, {"BCryptDestroyKey", s_Zero, 0}, {"BCryptDestroySecret", s_Zero, 0},
    {"BCryptEncrypt", s_BcryptNotSupported, 0}, {"BCryptExportKey", s_BcryptNotSupported, 0},
    {"BCryptFinalizeKeyPair", s_BcryptNotSupported, 0}, {"BCryptFinishHash", s_BcryptNotSupported, 0},
    {"BCryptGenerateKeyPair", s_BcryptNotSupported, 0}, {"BCryptGenerateSymmetricKey", s_BcryptNotSupported, 0},
    {"BCryptGetProperty", s_BcryptNotSupported, 0}, {"BCryptHashData", s_BcryptNotSupported, 0},
    {"BCryptImportKeyPair", s_BcryptNotSupported, 0}, {"BCryptOpenAlgorithmProvider", s_BcryptNotSupported, 0},
    {"BCryptSecretAgreement", s_BcryptNotSupported, 0}, {"BCryptSignHash", s_BcryptNotSupported, 0},
    {"BCryptVerifySignature", s_BcryptNotSupported, 0},
    {NULL, NULL, 0},
};
static const RcnShim setupapi_more[] = {{"SetupDiGetDeviceInstanceIdW", s_Zero, 0}, {NULL, NULL, 0}};
static const RcnShim usp10[] = {
    {"ScriptFreeCache", s_Zero, 0}, {"ScriptItemize", s_NotImpl, 0}, {"ScriptPlace", s_NotImpl, 0}, {"ScriptShape", s_NotImpl, 0},
    {NULL, NULL, 0},
};
// UuidToStringA(&uuid, &texte) : « xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx », libéré par RpcStringFreeA.
static void s_UuidToStringA(RcnCtx *ctx) {
    char *text = rcn_galloc(ctx, 37, 0);
    if (!A0 || !A1 || !text) {
        if (text) rcn_gfree(ctx, text);
        RET(14); // RPC_S_OUT_OF_MEMORY
        return;
    }
    u64 u = A0;
    snprintf(text, 37, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", rd32(u), rd16(u + 4), rd16(u + 6), rd8(u + 8),
             rd8(u + 9), rd8(u + 10), rd8(u + 11), rd8(u + 12), rd8(u + 13), rd8(u + 14), rd8(u + 15));
    wrp(ctx, A1, (u64)(uintptr_t)text);
    RET(0);
}
// RpcStringFreeA(&texte)
static void s_RpcStringFree(RcnCtx *ctx) {
    if (A0 && rdp(ctx, A0)) {
        rcn_gfree(ctx, PTR(rdp(ctx, A0)));
        wrp(ctx, A0, 0);
    }
    RET(0);
}
static const RcnShim rpcrt4[] = {
    {"UuidCreate", s_UuidCreate, S32(1)}, {"UuidToStringA", s_UuidToStringA, S32(2)},
    {"RpcStringFreeA", s_RpcStringFree, S32(1)}, {NULL, NULL, 0},
};
// ReportFault(exception, options) : rapport d'erreur de Windows, appelé par le gestionnaire de plantage d'un jeu
// (__except d'Unreal Engine 3) ; frrvErrNoDW (4) : pas de Dr Watson, le jeu continue son propre traitement.
static void s_ReportFault(RcnCtx *ctx) { RET(4); }
static const RcnShim faultrep[] = {{"ReportFault", s_ReportFault, S32(2)}, {NULL, NULL, 0}};

// --- security.dll / secur32.dll (SSPI) : interface présente, aucun paquet utilisable ---
// InitSecurityInterfaceA / W : table SecurityFunctionTable (version, puis 33 fonctions) dont chaque
// fonction répond SEC_E_UNSUPPORTED_FUNCTION (FreeContextBuffer : succès).
static void sec_Unsupported(RcnCtx *ctx) { RET(0x80090302u); }
static void sec_Ok(RcnCtx *ctx) { RET(0); }
static void sec_InitSecurityInterface(RcnCtx *ctx) {
    static u64 table;
    if (!table) {
        enum { COUNT = 33, FREE_CONTEXT_BUFFER = 16 };
        u8 *t = rcn_galloc(ctx, 8 + 8 * COUNT, 1);
        if (!t) {
            RET(0);
            return;
        }
        wr32((u64)(uintptr_t)t, 4); // SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION_4
        u64 unsupported = rcn_thunk(ctx, "security.dll", "SspiUnsupported", sec_Unsupported, 0);
        u64 ok = rcn_thunk(ctx, "security.dll", "FreeContextBuffer", sec_Ok, 0);
        for (int i = 0; i < COUNT; i++) wr64((u64)(uintptr_t)t + 8 + 8ull * (u64)i, i == FREE_CONTEXT_BUFFER ? ok : unsupported);
        table = (u64)(uintptr_t)t;
    }
    RET(table);
}
static const RcnShim security[] = {
    {"InitSecurityInterfaceA", sec_InitSecurityInterface, 0}, {"InitSecurityInterfaceW", sec_InitSecurityInterface, 0},
    {"FreeContextBuffer", sec_Ok, 0}, {"AcquireCredentialsHandleA", sec_Unsupported, 0},
    {"AcquireCredentialsHandleW", sec_Unsupported, 0}, {"EnumerateSecurityPackagesA", sec_Unsupported, 0},
    {"EnumerateSecurityPackagesW", sec_Unsupported, 0},
    {NULL, NULL, 0},
};


// --- Unreal Engine 5 : compléments sans objet sur l'appareil (dessin GDI, IME, accessibilité, réseau,
// certificats, périphériques) ---

// VARIANT (24 octets, 16 pour un jeu 32 bits) : type (u16) au début, valeur à 8.
#define VARIANT_SIZE(ctx) ((ctx)->bits32 ? 16 : 24)
static void v_VariantInit(RcnCtx *ctx) {
    if (A0) memset(PTR(A0), 0, VARIANT_SIZE(ctx));
}
static void v_VariantClear(RcnCtx *ctx) {
    if (A0) {
        if (rd16(A0) == 8 && rdp(ctx, A0 + 8)) rcn_gfree(ctx, PTR(rdp(ctx, A0 + 8) - 4)); // VT_BSTR
        memset(PTR(A0), 0, VARIANT_SIZE(ctx));
    }
    RET(0);
}
static void v_VariantCopy(RcnCtx *ctx) {
    if (!A0 || !A1) {
        RET(0x80070057u);
        return;
    }
    memcpy(PTR(A0), PTR(A1), VARIANT_SIZE(ctx));
    if (rd16(A1) == 8 && rdp(ctx, A1 + 8)) {
        u64 src = rdp(ctx, A1 + 8);
        wrp(ctx, A0 + 8, bstr(ctx, src, rd32(src - 4) / 2));
    }
    RET(0);
}
// GetErrorInfo(réservé, &info) : aucune (S_FALSE).
static void v_GetErrorInfo(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    RET(1);
}
// GetTextExtentPoint32W(dc, texte, n, &SIZE) : 8 × 16 pixels par caractère.
static void g_GetTextExtentPoint32W(RcnCtx *ctx) {
    if (A3) wr32(A3, 8 * (u32)A2), wr32(A3 + 4, 16);
    RET(1);
}
static void s_Two(RcnCtx *ctx) { RET(2); }
// DwmIsCompositionEnabled(&activée)
static void d_DwmIsCompositionEnabled(RcnCtx *ctx) {
    if (A0) wr32(A0, 1);
    RET(0);
}
// UiaHostProviderFromHwnd(fenêtre, &fournisseur)
static void ua_HostProvider(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    RET(0x80004005u);
}
// WinHttpGetDefaultProxyConfiguration(&WINHTTP_PROXY_INFO) : sans mandataire.
static void w_DefaultProxy(RcnCtx *ctx) {
    if (A0) {
        memset(PTR(A0), 0, 24);
        wr32(A0, 1); // WINHTTP_ACCESS_TYPE_NO_PROXY
    }
    RET(1);
}
// SystemFunction036 (RtlGenRandom)(tampon, taille)
static void a_RtlGenRandom(RcnCtx *ctx) {
    static u64 state = 0x9E3779B97F4A7C15ull;
    u8 *p = PTR(A0);
    for (u32 i = 0; i < (u32)A1; i++) {
        state ^= state << 13, state ^= state >> 7, state ^= state << 17;
        p[i] = (u8)(state >> 24);
    }
    RET(1);
}
// AllocateLocallyUniqueId(&LUID)
static void a_AllocateLocallyUniqueId(RcnCtx *ctx) {
    static u32 next = 0x1000;
    if (A0) wr32(A0, __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED)), wr32(A0 + 4, 0);
    RET(1);
}
static void s_CrFailure(RcnCtx *ctx) { RET(0x13); }  // CR_FAILURE
// CM_Get_Device_ID_List_SizeW(&taille, filtre, options) / CM_Get_Device_ID_ListW(filtre, tampon, taille,
// options) : liste vide (un seul zéro final). CM_MapCrToWin32Err(code, défaut) : le défaut.
static void cm_ListSize(RcnCtx *ctx) {
    if (A0) wr32(A0, 1);
    RET(0);
}
static void cm_List(RcnCtx *ctx) {
    if (A1 && (u32)A2) wr16(A1, 0);
    RET(0);
}
static void cm_MapCrToWin32Err(RcnCtx *ctx) { RET((u32)A1); }
// PathRemoveFileSpecW(chemin) : dernier composant retiré (TRUE si le chemin a changé).
static void sh_PathRemoveFileSpecW(RcnCtx *ctx) {
    if (!A0) {
        RET(0);
        return;
    }
    u16 *p = PTR(A0);
    size_t n = 0, cut = 0;
    while (p[n]) {
        if (p[n] == '\\' || p[n] == '/') cut = n;
        n++;
    }
    if (!n) {
        RET(0);
        return;
    }
    // « C:\fichier » : la barre après le lecteur reste ; sans barre, plus rien.
    if (cut == 2 && p[1] == ':') cut = 3;
    if (cut == 0 && p[0] != '\\' && p[0] != '/') cut = 0;
    else if (cut == 0) cut = 1;
    RET(cut < n);
    p[cut] = 0;
}
static void s_NoData(RcnCtx *ctx) { RET(232); }      // ERROR_NO_DATA
// oleaut32 par rang : VariantInit, VariantClear, VariantCopy, SafeArrayPutElement, GetErrorInfo,
// SafeArrayCreateVector. Table propre à oleaut32 (les rangs n'ont de sens que pour une bibliothèque).
// --- SAFEARRAY d'oleaut32 (interopérabilité COM de Mono et de .NET) ---
// cDims (u16), fFeatures (u16), cbElements, cLocks, pvData (adresse), puis les bornes {cElements, lLbound}
// de chaque dimension, de la dernière à la première. Le type des éléments (VARTYPE) est rangé 4 octets avant
// la structure (FADF_HAVEVARTYPE), dans un en-tête de 16 octets alloué avec elle.
#define SA_DATA(ctx) ((ctx)->bits32 ? 12u : 16u)
#define SA_BOUNDS(ctx) ((ctx)->bits32 ? 16u : 24u)
#define DISP_E_BADINDEX 0x8002000Bu
#define DISP_E_ARRAYISLOCKED 0x8002000Du
#define E_INVALIDARG 0x80070057u
enum { VT_BSTR = 8, VT_DISPATCH = 9, VT_VARIANT = 12, VT_UNKNOWN = 13 };
// Taille d'un élément du type vt et drapeaux FADF_* qui l'accompagnent ; 0 : type refusé.
static u32 sa_element(RcnCtx *ctx, u32 vt, u16 *features) {
    *features = 0x80; // FADF_HAVEVARTYPE
    switch (vt) {
    case 16: case 17: return 1;                                  // I1, UI1
    case 2: case 11: case 18: return 2;                          // I2, BOOL, UI2
    case 3: case 4: case 10: case 19: case 22: case 23: return 4; // I4, R4, ERROR, UI4, INT, UINT
    case 5: case 6: case 7: case 20: case 21: return 8;          // R8, CY, DATE, I8, UI8
    case 14: return 16;                                          // DECIMAL
    case VT_BSTR: *features |= 0x100; return PTR_SIZE(ctx);
    case VT_UNKNOWN: *features |= 0x200; return PTR_SIZE(ctx);
    case VT_DISPATCH: *features |= 0x400; return PTR_SIZE(ctx);
    case VT_VARIANT: *features |= 0x800; return VARIANT_SIZE(ctx);
    default: return 0;
    }
}
static u32 sa_dims(u64 sa) { return rd16(sa); }
static u32 sa_cb(u64 sa) { return rd32(sa + 4); }
static u32 sa_vt(u64 sa) { return rd32(sa - 4); }
// Nombre total d'éléments.
static u64 sa_count(RcnCtx *ctx, u64 sa) {
    u64 n = 1;
    for (u32 i = 0; i < sa_dims(sa); i++) n *= rd32(sa + SA_BOUNDS(ctx) + 8 * (u64)i);
    return n;
}
// SafeArrayCreate(vt, dimensions, &bornes) : bornes données de la première à la dernière dimension.
static void sa_SafeArrayCreate(RcnCtx *ctx) {
    u32 vt = (u32)A0 & 0xFFFF, dims = (u32)A1;
    u16 features;
    u32 cb = sa_element(ctx, vt, &features);
    if (!cb || !dims || dims > 64 || !A2) {
        RET(0);
        return;
    }
    u8 *block = rcn_galloc(ctx, 16 + SA_BOUNDS(ctx) + 8 * (size_t)dims, 1);
    if (!block) {
        RET(0);
        return;
    }
    u64 sa = (u64)(uintptr_t)block + 16, count = 1;
    wr32(sa - 4, vt);
    wr16(sa, (u16)dims), wr16(sa + 2, features), wr32(sa + 4, cb);
    for (u32 i = 0; i < dims; i++) {
        u64 to = sa + SA_BOUNDS(ctx) + 8 * (u64)(dims - 1 - i);
        wr32(to, rd32(A2 + 8 * (u64)i)), wr32(to + 4, rd32(A2 + 8 * (u64)i + 4));
        count *= rd32(A2 + 8 * (u64)i);
    }
    void *data = count ? rcn_galloc(ctx, (size_t)(count * cb), 1) : NULL;
    if (count && !data) {
        rcn_gfree(ctx, block);
        RET(0);
        return;
    }
    wrp(ctx, sa + SA_DATA(ctx), (u64)(uintptr_t)data);
    RET(sa);
}
// SafeArrayDestroy(tableau) : éléments (BSTR, VARIANT de texte) puis données et structure.
static void sa_SafeArrayDestroy(RcnCtx *ctx) {
    u64 sa = A0;
    if (!sa) {
        RET(0);
        return;
    }
    if (rd32(sa + 8)) {
        RET(DISP_E_ARRAYISLOCKED);
        return;
    }
    u64 data = rdp(ctx, sa + SA_DATA(ctx)), count = sa_count(ctx, sa);
    u32 cb = sa_cb(sa), vt = sa_vt(sa);
    for (u64 i = 0; data && i < count; i++) {
        u64 cell = data + i * cb;
        if (vt == VT_BSTR && rdp(ctx, cell)) rcn_gfree(ctx, PTR(rdp(ctx, cell) - 4));
        if (vt == VT_VARIANT && rd16(cell) == VT_BSTR && rdp(ctx, cell + 8)) rcn_gfree(ctx, PTR(rdp(ctx, cell + 8) - 4));
    }
    if (data) rcn_gfree(ctx, PTR(data));
    rcn_gfree(ctx, PTR(sa - 16));
    RET(0);
}
static void sa_SafeArrayGetDim(RcnCtx *ctx) { RET(A0 ? sa_dims(A0) : 0); }
static void sa_SafeArrayGetElemsize(RcnCtx *ctx) { RET(A0 ? sa_cb(A0) : 0); }
// SafeArrayGetLBound / GetUBound(tableau, dimension à partir de 1, &borne).
static void sa_bound(RcnCtx *ctx, int upper) {
    u64 sa = A0;
    u32 dim = (u32)A1;
    if (!sa || !A2) {
        RET(E_INVALIDARG);
        return;
    }
    if (dim < 1 || dim > sa_dims(sa)) {
        RET(DISP_E_BADINDEX);
        return;
    }
    u64 b = sa + SA_BOUNDS(ctx) + 8 * (u64)(sa_dims(sa) - dim);
    i32 lower = (i32)rd32(b + 4);
    wr32(A2, (u32)(upper ? lower + (i32)rd32(b) - 1 : lower));
    RET(0);
}
static void sa_SafeArrayGetLBound(RcnCtx *ctx) { sa_bound(ctx, 0); }
static void sa_SafeArrayGetUBound(RcnCtx *ctx) { sa_bound(ctx, 1); }
// Adresse de l'élément d'indices donnés (première dimension la plus rapide, comme Windows), 0 hors bornes.
static u64 sa_cell(RcnCtx *ctx, u64 sa, u64 indices) {
    u32 dims = sa_dims(sa);
    u64 cell = 0, stride = 1;
    for (u32 i = 0; i < dims; i++) {
        u64 b = sa + SA_BOUNDS(ctx) + 8 * (u64)(dims - 1 - i);
        i64 k = (i64)(i32)rd32(indices + 4 * (u64)i) - (i32)rd32(b + 4);
        if (k < 0 || k >= (i64)rd32(b)) return 0;
        cell += (u64)k * stride;
        stride *= rd32(b);
    }
    u64 data = rdp(ctx, sa + SA_DATA(ctx));
    return data ? data + cell * sa_cb(sa) : 0;
}
// SafeArrayPtrOfIndex(tableau, &indices, &adresse).
static void sa_SafeArrayPtrOfIndex(RcnCtx *ctx) {
    u64 cell = A0 && A1 ? sa_cell(ctx, A0, A1) : 0;
    if (!cell || !A2) {
        RET(A0 && A1 && A2 ? DISP_E_BADINDEX : E_INVALIDARG);
        return;
    }
    wrp(ctx, A2, cell);
    RET(0);
}
// SafeArrayPutElement / GetElement(tableau, &indices, valeur) : copie d'un élément (les BSTR sont
// dupliquées) ; VARIANT et interfaces ne sont pas pris en charge.
static void sa_element_copy(RcnCtx *ctx, int put) {
    u64 cell = A0 && A1 ? sa_cell(ctx, A0, A1) : 0;
    if (!cell || !A2) {
        RET(A0 && A1 && A2 ? DISP_E_BADINDEX : E_INVALIDARG);
        return;
    }
    u32 vt = sa_vt(A0), cb = sa_cb(A0);
    if (vt == VT_VARIANT || vt == VT_UNKNOWN || vt == VT_DISPATCH) {
        RET(0x80004001u); // E_NOTIMPL
        return;
    }
    if (vt == VT_BSTR) {
        u64 src = put ? A2 : rdp(ctx, cell);
        u64 copy = src ? bstr(ctx, src, rd32(src - 4) / 2) : 0;
        if (put) {
            if (rdp(ctx, cell)) rcn_gfree(ctx, PTR(rdp(ctx, cell) - 4));
            wrp(ctx, cell, copy);
        } else {
            wrp(ctx, A2, copy);
        }
        RET(0);
        return;
    }
    if (put) memcpy(PTR(cell), PTR(A2), cb);
    else memcpy(PTR(A2), PTR(cell), cb);
    RET(0);
}
static void sa_SafeArrayPutElement(RcnCtx *ctx) { sa_element_copy(ctx, 1); }
static void sa_SafeArrayGetElement(RcnCtx *ctx) { sa_element_copy(ctx, 0); }
// SafeArrayLock / Unlock / AccessData / UnaccessData : compteur de verrous.
static void sa_SafeArrayLock(RcnCtx *ctx) {
    if (A0) wr32(A0 + 8, rd32(A0 + 8) + 1);
    RET(A0 ? 0 : E_INVALIDARG);
}
static void sa_SafeArrayUnlock(RcnCtx *ctx) {
    if (A0 && rd32(A0 + 8)) wr32(A0 + 8, rd32(A0 + 8) - 1);
    RET(A0 ? 0 : E_INVALIDARG);
}
static void sa_SafeArrayAccessData(RcnCtx *ctx) {
    if (!A0 || !A1) {
        RET(E_INVALIDARG);
        return;
    }
    wr32(A0 + 8, rd32(A0 + 8) + 1);
    wrp(ctx, A1, rdp(ctx, A0 + SA_DATA(ctx)));
    RET(0);
}
// OleLoadPicture(flux, taille, mode, iid, &objet) : pas de décodeur d'images OLE.
static void o_OleLoadPicture(RcnCtx *ctx) {
    if (STK(4)) wrp(ctx, STK(4), 0);
    RET(0x80004001u); // E_NOTIMPL
}
static const RcnShim ue_oleaut32[] = {
    {"#418", o_OleLoadPicture, S32(5)}, {"OleLoadPicture", o_OleLoadPicture, S32(5)},
    {"#8", v_VariantInit, S32(1)}, {"#9", v_VariantClear, S32(1)}, {"#10", v_VariantCopy, S32(2)},
    // 15 SafeArrayCreate, 16 Destroy, 17 GetDim, 18 GetElemsize, 19 GetUBound, 20 GetLBound, 21 Lock, 22 Unlock,
    // 23 AccessData, 24 UnaccessData, 25 GetElement, 26 PutElement, 148 PtrOfIndex.
    {"#15", sa_SafeArrayCreate, S32(3)}, {"#16", sa_SafeArrayDestroy, S32(1)}, {"#17", sa_SafeArrayGetDim, S32(1)},
    {"#18", sa_SafeArrayGetElemsize, S32(1)}, {"#19", sa_SafeArrayGetUBound, S32(3)}, {"#20", sa_SafeArrayGetLBound, S32(3)},
    {"#21", sa_SafeArrayLock, S32(1)}, {"#22", sa_SafeArrayUnlock, S32(1)}, {"#23", sa_SafeArrayAccessData, S32(2)},
    {"#24", sa_SafeArrayUnlock, S32(1)}, {"#25", sa_SafeArrayGetElement, S32(3)}, {"#26", sa_SafeArrayPutElement, S32(3)},
    {"#148", sa_SafeArrayPtrOfIndex, S32(3)},
    {"SafeArrayCreate", sa_SafeArrayCreate, S32(3)}, {"SafeArrayDestroy", sa_SafeArrayDestroy, S32(1)},
    {"SafeArrayGetDim", sa_SafeArrayGetDim, S32(1)}, {"SafeArrayGetElemsize", sa_SafeArrayGetElemsize, S32(1)},
    {"SafeArrayGetUBound", sa_SafeArrayGetUBound, S32(3)}, {"SafeArrayGetLBound", sa_SafeArrayGetLBound, S32(3)},
    {"SafeArrayLock", sa_SafeArrayLock, S32(1)}, {"SafeArrayUnlock", sa_SafeArrayUnlock, S32(1)},
    {"SafeArrayAccessData", sa_SafeArrayAccessData, S32(2)}, {"SafeArrayUnaccessData", sa_SafeArrayUnlock, S32(1)},
    {"SafeArrayGetElement", sa_SafeArrayGetElement, S32(3)}, {"SafeArrayPutElement", sa_SafeArrayPutElement, S32(3)},
    {"SafeArrayPtrOfIndex", sa_SafeArrayPtrOfIndex, S32(3)},
    {"#200", v_GetErrorInfo, 0}, {"#411", s_Zero, 0}, {"VariantInit", v_VariantInit, S32(1)},
    {"VariantClear", v_VariantClear, S32(1)}, {"VariantCopy", v_VariantCopy, S32(2)}, {"GetErrorInfo", v_GetErrorInfo, 0},
    {NULL, NULL, 0},
};
// --- Régions rectangulaires de gdi32 ---
// Unreal Engine teste « le pointeur est-il dans ma fenêtre ? » par CreateRectRgn + PtInRegion
// (FWindowsWindow::IsPointInWindow) : à faux, Slate ne trouvait aucun widget sous le pointeur, et ni le
// survol, ni le clic, ni la validation à la manette (clic simulé par CommonUI) n'aboutissaient (Clair
// Obscur : « Nouvelle partie » sans effet). Chaque région est un rectangle (les arrondis et combinaisons
// sont ramenés à leur cadre), dans une table circulaire : handle = 0x7E000000 + rang.
enum { RGN_SLOTS = 4096, RGN_BASE = 0x7E000000, RGN_AND = 1, RGN_OR = 2, RGN_XOR = 3, RGN_DIFF = 4, RGN_COPY = 5,
       NULLREGION = 1, SIMPLEREGION = 2 };
static struct {
    i32 l, t, r, b;
} regions[RGN_SLOTS];
static volatile u32 region_next;
static u64 region_new(i32 l, i32 t, i32 r, i32 b) {
    u32 k = __atomic_fetch_add(&region_next, 1, __ATOMIC_RELAXED) % RGN_SLOTS;
    if (r < l) { i32 x = l; l = r; r = x; }
    if (b < t) { i32 x = t; t = b; b = x; }
    regions[k].l = l, regions[k].t = t, regions[k].r = r, regions[k].b = b;
    return RGN_BASE + k;
}
static int region_of(u64 h) { return h >= RGN_BASE && h < RGN_BASE + RGN_SLOTS ? (int)(h - RGN_BASE) : -1; }
static void g_CreateRectRgn(RcnCtx *ctx) { RET(region_new((i32)A0, (i32)A1, (i32)A2, (i32)A3)); }
static void g_CreateRectRgnIndirect(RcnCtx *ctx) {
    RET(A0 ? region_new((i32)rd32(A0), (i32)rd32(A0 + 4), (i32)rd32(A0 + 8), (i32)rd32(A0 + 12)) : 0);
}
// CombineRgn(destination, source1, source2, mode) : cadres combinés.
static void g_CombineRgn(RcnCtx *ctx) {
    int d = region_of(A0), a = region_of(A1), b = region_of(A2);
    u32 mode = (u32)A3;
    if (d < 0 || a < 0) {
        RET(0); // ERROR
        return;
    }
    i32 l = regions[a].l, t = regions[a].t, r = regions[a].r, bo = regions[a].b;
    if (b >= 0 && mode == RGN_AND) {
        if (regions[b].l > l) l = regions[b].l;
        if (regions[b].t > t) t = regions[b].t;
        if (regions[b].r < r) r = regions[b].r;
        if (regions[b].b < bo) bo = regions[b].b;
        if (r < l) r = l;
        if (bo < t) bo = t;
    } else if (b >= 0 && (mode == RGN_OR || mode == RGN_XOR)) {
        if (regions[b].l < l) l = regions[b].l;
        if (regions[b].t < t) t = regions[b].t;
        if (regions[b].r > r) r = regions[b].r;
        if (regions[b].b > bo) bo = regions[b].b;
    }
    regions[d].l = l, regions[d].t = t, regions[d].r = r, regions[d].b = bo;
    RET(r > l && bo > t ? SIMPLEREGION : NULLREGION);
}
static void g_PtInRegion(RcnCtx *ctx) {
    int k = region_of(A0);
    i32 x = (i32)A1, y = (i32)A2;
    RET(k >= 0 && x >= regions[k].l && x < regions[k].r && y >= regions[k].t && y < regions[k].b);
}
static void g_GetRgnBox(RcnCtx *ctx) {
    int k = region_of(A0);
    if (k < 0 || !A1) {
        RET(0);
        return;
    }
    wr32(A1, (u32)regions[k].l), wr32(A1 + 4, (u32)regions[k].t), wr32(A1 + 8, (u32)regions[k].r), wr32(A1 + 12, (u32)regions[k].b);
    RET(regions[k].r > regions[k].l && regions[k].b > regions[k].t ? SIMPLEREGION : NULLREGION);
}
static void g_OffsetRgn(RcnCtx *ctx) {
    int k = region_of(A0);
    if (k < 0) {
        RET(0);
        return;
    }
    regions[k].l += (i32)A1, regions[k].r += (i32)A1, regions[k].t += (i32)A2, regions[k].b += (i32)A2;
    RET(SIMPLEREGION);
}
// CreateDIBSection(dc, &BITMAPINFO, usage, &bits, section, décalage) : pixels dans la mémoire du jeu,
// d'après l'en-tête (largeur, hauteur, bits par pixel ; lignes alignées sur 4 octets), adresse rendue dans
// *bits et bitmap factice rendu. Le jeu y écrit ses pixels (curseur d'Unity) : sans mémoire, il écrivait à
// l'adresse restée dans sa variable locale, en pleine pile.
static void g_CreateDIBSection(RcnCtx *ctx) {
    u64 info = A1, bits = A3;
    if (bits) wrp(ctx, bits, 0);
    if (!info) {
        RET(0);
        return;
    }
    i32 width = (i32)rd32(info + 4), height = (i32)rd32(info + 8);
    u32 bpp = rd16(info + 14);
    if (width <= 0 || !height || !bpp || bpp > 32 || width > 16384 || height > 16384 || height < -16384) {
        RET(0);
        return;
    }
    u32 rows = (u32)(height < 0 ? -height : height);
    size_t stride = (((size_t)width * bpp + 31) / 32) * 4;
    void *pixels = rcn_galloc(ctx, stride * rows, 1);
    if (!pixels) {
        RET(0);
        return;
    }
    if (bits) wrp(ctx, bits, (u64)(uintptr_t)pixels);
    RET(0xB17A0000u | (((uintptr_t)pixels >> 4) & 0xFFFF)); // HBITMAP factice
}
// Noms de fonctions de plusieurs bibliothèques (aucun rang ici).
static const RcnShim ue_sys[] = {
    // gdi32
    {"CreateCompatibleDC", s_True, S32(1)}, {"DeleteDC", s_True, S32(1)}, {"CreateDIBSection", g_CreateDIBSection, S32(6)},
    {"CreateBitmap", s_True, S32(5)}, {"CreateFontW", s_True, 0}, {"CreatePen", s_True, 0},
    {"CreateSolidBrush", s_True, 0}, {"CreateRectRgn", g_CreateRectRgn, 0}, {"CreateRoundRectRgn", g_CreateRectRgn, 0},
    {"CreateRectRgnIndirect", g_CreateRectRgnIndirect, 0}, {"GetRgnBox", g_GetRgnBox, 0}, {"OffsetRgn", g_OffsetRgn, 0},
    {"CombineRgn", g_CombineRgn, 0}, {"PtInRegion", g_PtInRegion, 0}, {"CreateDCW", s_True, 0}, {"ExtTextOutW", s_True, 0},
    {"TextOutW", s_True, S32(5)}, {"GetTextExtentPoint32W", g_GetTextExtentPoint32W, S32(4)}, {"LineTo", s_True, 0},
    {"MoveToEx", s_True, 0}, {"Polygon", s_True, 0}, {"Rectangle", s_True, 0}, {"RoundRect", s_True, 0},
    {"SetBkColor", s_Zero, S32(2)}, {"SetBkMode", s_True, 0}, {"SetTextAlign", s_Zero, 0}, {"SetTextColor", s_Zero, 0},
    {"GetICMProfileW", s_Zero, 0},
    // imm32
    {"ImmCreateContext", s_True, 0}, {"ImmDestroyContext", s_True, 0}, {"ImmGetDescriptionW", s_Zero, 0},
    {"ImmGetIMEFileNameW", s_Zero, 0}, {"ImmGetProperty", s_Zero, 0}, {"ImmSetCandidateWindow", s_True, 0},
    {"ImmSetCompositionWindow", s_True, 0},
    // iphlpapi
    {"GetAdaptersInfo", s_NoData, S32(2)}, {"IcmpCloseHandle", s_True, 0},
    // setupapi
    {"CM_Get_Device_IDW", s_CrFailure, 0}, {"CM_Get_DevNode_PropertyW", s_CrFailure, 0},
    {"CM_Locate_DevNodeW", s_CrFailure, 0}, {"CM_Get_Device_ID_List_SizeW", cm_ListSize, 0},
    {"CM_Get_Device_ID_ListW", cm_List, 0}, {"CM_MapCrToWin32Err", cm_MapCrToWin32Err, 0},
    {"SetupGetInfDriverStoreLocationW", s_Zero, 0}, {"PathRemoveFileSpecW", sh_PathRemoveFileSpecW, 0},
    // avrt : priorités « multimédia » des threads, acceptées sans effet.
    {"AvSetMmThreadCharacteristicsA", s_True, 0}, {"AvSetMmThreadCharacteristicsW", s_True, 0},
    {"AvRevertMmThreadCharacteristics", s_True, 0}, {"AvSetMmThreadPriority", s_True, 0}, {"SetupDiGetClassDevsExW", s_InvalidHandle, 0},
    {"SetupDiGetDevicePropertyW", s_Zero, 0}, {"SetupDiGetDeviceRegistryPropertyW", s_Zero, 0},
    {"SetupDiOpenDevRegKey", s_InvalidHandle, 0},
    // shell32, uxtheme, UIAutomationCore
    {"ShellExecuteExW", s_Zero, S32(1)}, {"SetWindowTheme", s_Zero, 0},
    {"UiaClientsAreListening", s_Zero, 0}, {"UiaHostProviderFromHwnd", ua_HostProvider, 0},
    {"UiaRaiseAutomationEvent", s_Zero, 0}, {"UiaRaiseAutomationPropertyChangedEvent", s_Zero, 0},
    {"UiaReturnRawElementProvider", s_Zero, 0},
    // winhttp, ws2_32
    {"WinHttpCrackUrl", s_Zero, 0}, {"WinHttpGetDefaultProxyConfiguration", w_DefaultProxy, 0},
    {"InetNtopW", s_Zero, 0},
    // dwmapi
    {"DwmFlush", s_Zero, 0}, {"DwmGetCompositionTimingInfo", s_Fail, 0},
    {"DwmIsCompositionEnabled", d_DwmIsCompositionEnabled, 0},
    // ole32
    {"CoInitializeSecurity", s_Zero, 0}, {"IIDFromString", s_Fail, 0}, {"OleInitialize", s_Zero, 0},
    {"OleUninitialize", s_Nothing, 0}, {"RegisterDragDrop", s_Zero, 0}, {"RevokeDragDrop", s_Zero, 0},
    {"ReleaseStgMedium", s_Nothing, 0},
    // advapi32 : hasard, identifiants ; sécurité et chiffrement par CryptoAPI indisponibles.
    {"SystemFunction036", a_RtlGenRandom, 0}, {"AllocateLocallyUniqueId", a_AllocateLocallyUniqueId, 0},
    {"BuildExplicitAccessWithNameW", s_Nothing, 0}, {"BuildSecurityDescriptorW", s_True, 0},
    {"ConvertStringSecurityDescriptorToSecurityDescriptorW", s_Zero, 0}, {"CryptDecrypt", s_Zero, 0},
    {"CryptDestroyKey", s_True, S32(1)}, {"CryptEnumProvidersW", s_Zero, 0}, {"CryptExportKey", s_Zero, 0},
    {"CryptGetProvParam", s_Zero, 0}, {"CryptGetUserKey", s_Zero, 0}, {"CryptSetHashParam", s_Zero, 0},
    {"CryptSignHashW", s_Zero, 0}, {"RegSetKeyValueW", s_Zero, 0},
    // crypt32 : magasins de certificats vides.
    {"CertDuplicateCertificateContext", s_Zero, 0}, {"CertEnumCertificatesInStore", s_Zero, 0},
    {"CertGetCertificateContextProperty", s_Zero, 0}, {"CertGetEnhancedKeyUsage", s_Zero, 0},
    {"CertGetIntendedKeyUsage", s_Zero, 0}, {"CertGetNameStringW", s_Zero, 0},
    {"CertOpenSystemStoreA", s_Zero, 0}, {"CertOpenSystemStoreW", s_Zero, 0},
    {NULL, NULL, 0},
};

// --- Compléments de plusieurs bibliothèques (Unity 5 et Mono : Ori and the Blind Forest) ---
static void a_NoMoreItems(RcnCtx *ctx) { RET(259); } // ERROR_NO_MORE_ITEMS
// DllGetVersion(&info) : { taille, majeure, mineure, construction, plateforme } (DLLVERSIONINFO).
static void sh_DllGetVersion(RcnCtx *ctx) {
    if (!A0 || rd32(A0) < 20) {
        RET(0x80070057u); // E_INVALIDARG
        return;
    }
    wr32(A0 + 4, 6), wr32(A0 + 8, 1), wr32(A0 + 12, 7601), wr32(A0 + 16, 2); // DLLVER_PLATFORM_NT
    RET(0);
}

// advapi32 : SID, jetons et ACL. Un seul utilisateur (« Player ») ; les fichiers du jeu sont à lui et sans
// restriction (ACL nulle : tout est permis, comme pour un dossier sans protection).
// SID : révision (1), nombre de sous-autorités, autorité (6 octets), sous-autorités (4 octets chacune).
static u32 sid_length(u64 sid) { return sid ? 8 + 4 * (u32)rd8(sid + 1) : 0; }
// AllocateAndInitializeSid(&autorité, nombre, a0 … a7, &sid)
static void a_AllocateAndInitializeSid(RcnCtx *ctx) {
    u32 count = (u32)A1 & 0xFF;
    if (count > 8 || !A0 || !STK(10)) {
        LAST_ERROR(ctx) = 87; // ERROR_INVALID_PARAMETER
        RET(0);
        return;
    }
    u8 *sid = rcn_galloc(ctx, 8 + 4 * (size_t)count, 1);
    if (!sid) {
        LAST_ERROR(ctx) = 8; // ERROR_NOT_ENOUGH_MEMORY
        RET(0);
        return;
    }
    u64 at = (u64)(uintptr_t)sid;
    wr8(at, 1), wr8(at + 1, (u8)count);
    memcpy(sid + 2, PTR(A0), 6);
    for (u32 i = 0; i < count; i++) wr32(at + 8 + 4 * (u64)i, (u32)STK(2 + (int)i));
    wrp(ctx, STK(10), at);
    RET(1);
}
static void a_FreeSid(RcnCtx *ctx) {
    if (A0) rcn_gfree(ctx, PTR(A0));
    RET(0);
}
static void a_GetLengthSid(RcnCtx *ctx) { RET(sid_length(A0)); }
// CopySid(taille, destination, source)
static void a_CopySid(RcnCtx *ctx) {
    u32 n = sid_length(A2);
    if (!n || !A1 || (u32)A0 < n) {
        LAST_ERROR(ctx) = 122; // ERROR_INSUFFICIENT_BUFFER
        RET(0);
        return;
    }
    memcpy(PTR(A1), PTR(A2), n);
    RET(1);
}
// Texte copié dans un tampon de caractères dont la taille est en *size_at (mise à jour, zéro compris si
// trop petit, sans lui sinon) ; 0 si le tampon est trop petit.
static int put_counted(u64 buffer, u64 size_at, const char *text, int wide) {
    u32 len = (u32)strlen(text), size = size_at ? rd32(size_at) : 0;
    if (!buffer || size <= len) {
        if (size_at) wr32(size_at, len + 1);
        return 0;
    }
    for (u32 i = 0; i <= len; i++) {
        if (wide) wr16(buffer + 2 * (u64)i, (u8)text[i]);
        else wr8(buffer + i, (u8)text[i]);
    }
    wr32(size_at, len);
    return 1;
}
// LookupAccountSidW(système, sid, nom, &taille, domaine, &taille, &genre) : l'utilisateur du jeu.
static void a_LookupAccountSidW(RcnCtx *ctx) {
    int ok = put_counted(A2, A3, "Player", 1);
    ok &= put_counted(STK(4), STK(5), "ROMCLOUD", 1);
    if (!ok) {
        LAST_ERROR(ctx) = 122;
        RET(0);
        return;
    }
    if (STK(6)) wr32(STK(6), 1); // SidTypeUser
    RET(1);
}
// BuildTrusteeWithSidW(&trustee, sid) : TRUSTEE_W { pMultipleTrustee, opération, forme, genre, ptstrName }.
static void a_BuildTrusteeWithSidW(RcnCtx *ctx) {
    if (!A0) return;
    u32 p = PTR_SIZE(ctx);
    wrp(ctx, A0, 0);
    wr32(A0 + p, 0);     // NO_MULTIPLE_TRUSTEE
    wr32(A0 + p + 4, 0); // TRUSTEE_IS_SID
    wr32(A0 + p + 8, 0); // TRUSTEE_IS_UNKNOWN
    wrp(ctx, A0 + (ctx->bits32 ? 16 : 24), A1);
}
// GetEffectiveRightsFromAclW(acl, &trustee, &droits) : tous les droits.
static void a_GetEffectiveRightsFromAclW(RcnCtx *ctx) {
    if (A2) wr32(A2, 0x001F01FFu); // FILE_ALL_ACCESS
    RET(0);
}
// GetNamedSecurityInfoW(nom, genre, informations, &propriétaire, &groupe, &dacl, &sacl, &descripteur) :
// descripteur (à libérer par LocalFree) dont propriétaire et groupe sont l'utilisateur, sans ACL.
static void a_GetNamedSecurityInfoW(RcnCtx *ctx) {
    u8 *block = rcn_galloc(ctx, 64, 1);
    if (!block) {
        RET(8);
        return;
    }
    u64 sd = (u64)(uintptr_t)block, sid = sd + 40;
    wr8(sd, 1);                 // révision
    wr16(sd + 2, 0x8000 | 0x4); // SE_SELF_RELATIVE | SE_DACL_PRESENT (DACL nulle : accès complet)
    wr32(sd + 4, 40), wr32(sd + 8, 40);
    // S-1-5-32-545 (utilisateurs) : propriétaire et groupe.
    wr8(sid, 1), wr8(sid + 1, 1), wr8(sid + 7, 5), wr32(sid + 8, 545);
    if (A3) wrp(ctx, A3, sid);
    if (STK(4)) wrp(ctx, STK(4), sid);
    if (STK(5)) wrp(ctx, STK(5), 0);
    if (STK(6)) wrp(ctx, STK(6), 0);
    if (STK(7)) wrp(ctx, STK(7), sd);
    else rcn_gfree(ctx, block);
    RET(0);
}
// SetEntriesInAclW(nombre, entrées, ancienne, &nouvelle) : ACL nulle (aucune restriction).
static void a_SetEntriesInAclW(RcnCtx *ctx) {
    if (A3) wrp(ctx, A3, 0);
    RET(0);
}
// OpenThreadToken : le thread n'emprunte aucune identité (ERROR_NO_TOKEN), comme sous Windows.
static void a_OpenThreadToken(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 1008;
    RET(0);
}
// DuplicateToken(jeton, niveau, &copie) : le même jeton.
static void a_DuplicateToken(RcnCtx *ctx) {
    if (A2) wrp(ctx, A2, A0);
    RET(A2 != 0);
}
// Pas d'autre processus ni d'autre compte.
static void a_CreateProcessWithLogonW(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 50; // ERROR_NOT_SUPPORTED
    RET(0);
}
// CryptoAPI sans fournisseur de clés : clé refusée, signature non vérifiée.
static void a_CryptImportKey(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 0x80090003u; // NTE_BAD_KEY
    RET(0);
}
static void a_CryptVerifySignature(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 0x80090006u; // NTE_BAD_SIGNATURE
    RET(0);
}
// Journal des événements : messages écrits dans le journal du jeu (RCN_TRACE).
static void a_RegisterEventSourceA(RcnCtx *ctx) { RET(0x7401); }
static void a_ReportEventA(RcnCtx *ctx) {
    u32 strings = (u32)STK(5);
    u64 list = STK(7);
    for (u32 i = 0; ctx->proc->trace && list && i < strings && i < 8; i++) {
        u64 text = rdp(ctx, list + PTR_SIZE(ctx) * (u64)i);
        if (text) rcn_log("ReportEvent : %s", STR(text));
    }
    RET(1);
}

// dnsapi : hors ligne, aucun nom n'est trouvé.
static void d_DnsQuery(RcnCtx *ctx) {
    if (STK(4)) wrp(ctx, STK(4), 0);
    RET(9003); // DNS_ERROR_RCODE_NAME_ERROR
}

// iphlpapi : GetIpAddrTable(&table, &taille, ordre) : la seule adresse est la boucle locale.
static void i_GetIpAddrTable(RcnCtx *ctx) {
    const u32 need = 4 + 24;
    if (!A1) {
        RET(87);
        return;
    }
    if (!A0 || rd32(A1) < need) {
        wr32(A1, need);
        RET(122); // ERROR_INSUFFICIENT_BUFFER
        return;
    }
    memset(PTR(A0), 0, need);
    wr32(A0, 1);
    wr32(A0 + 4, 0x0100007Fu);  // 127.0.0.1 (ordre réseau)
    wr32(A0 + 8, 1);            // interface
    wr32(A0 + 12, 0x000000FFu); // 255.0.0.0
    wr32(A0 + 16, 1);           // diffusion
    wr32(A0 + 20, 65535);       // taille de réassemblage
    wr16(A0 + 26, 1);           // MIB_IPADDR_PRIMARY
    RET(0);
}

// mswsock : TransmitFile, sans réseau.
static void n_TransmitFile(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 10050; // WSAENETDOWN
    RET(0);
}

// ole32 : mémoire des tâches, table des objets en cours et monikers (absents).
static void o_CoTaskMemRealloc(RcnCtx *ctx) { RET((u64)(uintptr_t)rcn_grealloc(ctx, PTR(A0), (size_t)A1)); }
// GetRunningObjectTable(réservé, &table) ; CreateItemMoniker(séparateur, nom, &moniker) : E_NOTIMPL, objet nul.
static void o_GetRunningObjectTable(RcnCtx *ctx) {
    if (A1) wrp(ctx, A1, 0);
    RET(0x80004001u);
}
static void o_CreateItemMoniker(RcnCtx *ctx) {
    if (A2) wrp(ctx, A2, 0);
    RET(0x80004001u);
}

// opengl32 sans contexte : wglCreateContext échoue (Direct3D est le seul rendu des jeux 32 bits), les appels
// de dessin sont donc faits sans contexte courant. Comme sous Windows dans ce cas, ils sont ignorés,
// glGetError rend GL_NO_ERROR et glGetString NULL ; les tampons des glGet* restent intacts.
static const RcnShim ori_sys[] = {
    // advapi32
    {"AllocateAndInitializeSid", a_AllocateAndInitializeSid, S32(11)}, {"FreeSid", a_FreeSid, S32(1)},
    {"GetLengthSid", a_GetLengthSid, S32(1)}, {"CopySid", a_CopySid, S32(3)},
    {"LookupAccountSidW", a_LookupAccountSidW, S32(7)}, {"BuildTrusteeWithSidW", a_BuildTrusteeWithSidW, S32(2)},
    {"GetEffectiveRightsFromAclW", a_GetEffectiveRightsFromAclW, S32(3)},
    {"GetNamedSecurityInfoW", a_GetNamedSecurityInfoW, S32(8)}, {"SetNamedSecurityInfoW", s_Zero, S32(7)},
    {"SetEntriesInAclW", a_SetEntriesInAclW, S32(4)}, {"OpenThreadToken", a_OpenThreadToken, S32(4)},
    {"DuplicateToken", a_DuplicateToken, S32(3)}, {"ImpersonateLoggedOnUser", s_True, S32(1)},
    {"RevertToSelf", s_True, S32(0)}, {"CreateProcessWithLogonW", a_CreateProcessWithLogonW, S32(11)},
    {"CryptImportKey", a_CryptImportKey, S32(6)}, {"CryptVerifySignatureA", a_CryptVerifySignature, S32(6)},
    {"CryptVerifySignatureW", a_CryptVerifySignature, S32(6)}, {"RegisterEventSourceA", a_RegisterEventSourceA, S32(2)},
    {"RegisterEventSourceW", a_RegisterEventSourceA, S32(2)}, {"ReportEventA", a_ReportEventA, S32(9)},
    // dnsapi
    {"DnsQuery_A", d_DnsQuery, S32(6)}, {"DnsQuery_W", d_DnsQuery, S32(6)}, {"DnsQuery_UTF8", d_DnsQuery, S32(6)},
    {"DnsFree", s_Nothing, S32(2)}, {"DnsRecordListFree", s_Nothing, S32(2)},
    // gdi32, imm32
    {"GetObjectA", s_Zero, S32(3)},
    // shell32 : DllGetVersion(&DLLVERSIONINFO) — version de Windows 7.
    {"DllGetVersion", sh_DllGetVersion, S32(1)},
    // advapi32 : clés du registre (aucune sous-clé, rien à supprimer).
    {"RegDeleteKeyA", s_NotFound, S32(2)}, {"RegDeleteKeyW", s_NotFound, S32(2)},
    {"RegEnumKeyA", a_NoMoreItems, S32(4)}, {"RegEnumKeyW", a_NoMoreItems, S32(4)},
    // gdi32 : dessin sur les DC des fenêtres (Bink en mode GDI) ; rien n'est affiché par GDI.
    {"GetPixel", s_MinusOne, S32(3)}, {"SetPixel", s_MinusOne, S32(4)}, {"SetStretchBltMode", s_True, S32(2)},
    {"StretchBlt", s_Zero, S32(11)}, {"ImmSetOpenStatus", s_True, S32(2)}, {"ImmGetOpenStatus", s_Zero, S32(1)},
    // iphlpapi, mswsock
    {"GetIpAddrTable", i_GetIpAddrTable, S32(3)}, {"TransmitFile", n_TransmitFile, S32(7)},
    // ole32
    {"CoTaskMemRealloc", o_CoTaskMemRealloc, S32(2)}, {"CoFreeUnusedLibraries", s_Nothing, S32(0)},
    {"CoFreeUnusedLibrariesEx", s_Nothing, S32(2)}, {"GetRunningObjectTable", o_GetRunningObjectTable, S32(2)},
    {"CreateItemMoniker", o_CreateItemMoniker, S32(3)},
    // opengl32
    {"wglShareLists", s_Zero, S32(2)}, {"glGetError", s_Zero, S32(0)}, {"glGetString", s_Zero, S32(1)},
    {"glIsTexture", s_Zero, S32(1)}, {"glFinish", s_Nothing, S32(0)}, {"glFlush", s_Nothing, S32(0)},
    {"glLoadIdentity", s_Nothing, S32(0)},
    {"glBindTexture", s_Nothing, S32(2)}, {"glBlendFunc", s_Nothing, S32(2)}, {"glClear", s_Nothing, S32(1)},
    {"glClearColor", s_Nothing, S32(4)}, {"glClearDepth", s_Nothing, S32(2)}, {"glClearStencil", s_Nothing, S32(1)},
    {"glColor4f", s_Nothing, S32(4)}, {"glColorMask", s_Nothing, S32(4)}, {"glColorPointer", s_Nothing, S32(4)},
    {"glCopyTexSubImage2D", s_Nothing, S32(8)}, {"glCullFace", s_Nothing, S32(1)}, {"glDeleteTextures", s_Nothing, S32(2)},
    {"glDepthFunc", s_Nothing, S32(1)}, {"glDepthMask", s_Nothing, S32(1)}, {"glDisable", s_Nothing, S32(1)},
    {"glDisableClientState", s_Nothing, S32(1)}, {"glDrawArrays", s_Nothing, S32(3)}, {"glDrawBuffer", s_Nothing, S32(1)},
    {"glDrawElements", s_Nothing, S32(4)}, {"glEnable", s_Nothing, S32(1)}, {"glEnableClientState", s_Nothing, S32(1)},
    {"glFrontFace", s_Nothing, S32(1)}, {"glGenTextures", s_Nothing, S32(2)}, {"glGetBooleanv", s_Nothing, S32(2)},
    {"glGetFloatv", s_Nothing, S32(2)}, {"glGetIntegerv", s_Nothing, S32(2)},
    {"glGetTexLevelParameteriv", s_Nothing, S32(4)}, {"glGetTexParameteriv", s_Nothing, S32(3)},
    {"glLoadMatrixf", s_Nothing, S32(1)}, {"glMatrixMode", s_Nothing, S32(1)}, {"glMultMatrixf", s_Nothing, S32(1)},
    {"glNormalPointer", s_Nothing, S32(3)}, {"glPixelStorei", s_Nothing, S32(2)}, {"glPolygonMode", s_Nothing, S32(2)},
    {"glPolygonOffset", s_Nothing, S32(2)}, {"glReadBuffer", s_Nothing, S32(1)}, {"glReadPixels", s_Nothing, S32(7)},
    {"glScissor", s_Nothing, S32(4)}, {"glStencilMask", s_Nothing, S32(1)}, {"glTexCoordPointer", s_Nothing, S32(4)},
    {"glTexImage2D", s_Nothing, S32(9)}, {"glTexParameterf", s_Nothing, S32(3)}, {"glTexParameteri", s_Nothing, S32(3)},
    {"glTexSubImage2D", s_Nothing, S32(9)}, {"glVertexPointer", s_Nothing, S32(4)}, {"glViewport", s_Nothing, S32(4)},
    {NULL, NULL, 0},
};

extern const RcnShim rcn_mf_x64[];
extern const RcnShim rcn_d3dx9_math[], rcn_dinput8[], rcn_dsound_x64[], rcn_keyboard_user32[];
// Jeux 32 bits (bits32) : seulement les fonctions marquées prêtes (args32), *pop : octets d'arguments retirés.
static RcnFn sys_lookup(const char *dll, const char *name, int bits32, u32 *pop);
RcnFn rcn_sys_x64_lookup(const char *dll, const char *name) {
    u32 pop = 0;
    return sys_lookup(dll, name, 0, &pop);
}
RcnFn rcn_sys_x86_lookup(const char *dll, const char *name, u32 *pop) { return sys_lookup(dll, name, 1, pop); }
static RcnFn sys_lookup(const char *dll, const char *name, int bits32, u32 *pop) {
    static const struct {
        const char *dll;
        const RcnShim *table;
    } tables[] = {
        {"advapi32", advapi32}, {"crypt32", crypt32}, {"dbghelp", dbghelp},   {"dwmapi", dwmapi},
        {"gdi32", gdi32},       {"hid", hid},         {"imm32", imm32},       {"iphlpapi", iphlpapi},
        {"oleaut32", oleaut32}, {"ole32", ole32},     {"opengl32", opengl32}, {"setupapi", setupapi},
        {"shell32", shell32},   {"shlwapi", shlwapi}, {"version", version},   {"winhttp", winhttp},
        {"winmm", winmm},       {"ws2_32", ws2},        {"powrprof", powrprof},
        {"advapi32", advapi32_more}, {"crypt32", crypt32_more}, {"gdi32", gdi32_more}, {"iphlpapi", iphlpapi_more},
        {"wininet", wininet},   {"ws2_32", ws2_more}, {"mswsock", mswsock}, {"bcrypt", bcrypt_more},
        {"setupapi", setupapi_more}, {"usp10", usp10}, {"rpcrt4", rpcrt4}, {"faultrep", faultrep}, {"security", security}, {"secur32", security},
        {"gdi32", more_sys}, {"advapi32", more_sys}, {"shell32", more_sys}, {"hid", more_sys}, {"winhttp", more_sys},
        {"winmm", more_sys}, {"ws2_32", more_sys}, {"ole32", more_sys}, {"dinput8", more_sys},
        {"oleaut32", ue_oleaut32}, {"gdi32", ue_sys}, {"imm32", ue_sys}, {"iphlpapi", ue_sys}, {"setupapi", ue_sys},
        {"shell32", ue_sys}, {"uxtheme", ue_sys}, {"uiautomationcore", ue_sys}, {"winhttp", ue_sys}, {"ws2_32", ue_sys},
        {"dwmapi", ue_sys}, {"ole32", ue_sys}, {"avrt", ue_sys}, {"shlwapi", ue_sys}, {"advapi32", ue_sys}, {"crypt32", ue_sys}, {"cfgmgr32", ue_sys},
        {"advapi32", ori_sys}, {"dnsapi", ori_sys}, {"gdi32", ori_sys}, {"imm32", ori_sys}, {"iphlpapi", ori_sys},
        {"mswsock", ori_sys}, {"ole32", ori_sys}, {"opengl32", ori_sys}, {"shell32", ori_sys},
        {"mf", rcn_mf_x64},
        {"d3dx9", rcn_d3dx9_math}, {"dinput8", rcn_dinput8}, {"dsound", rcn_dsound_x64}, {"user32", rcn_keyboard_user32},
    };
    for (size_t i = 0; i < sizeof tables / sizeof tables[0]; i++) {
        if (!starts_with(dll, tables[i].dll)) continue;
        for (const RcnShim *s = tables[i].table; s->name; s++) {
            if (strcmp(s->name, name) != 0) continue;
            if (!bits32) return s->fn;
            if (!s->args32) return NULL;
            *pop = s->args32 == C32 ? 0 : (u32)(s->args32 - 1) * 4;
            return s->fn;
        }
    }
    return NULL;
}
