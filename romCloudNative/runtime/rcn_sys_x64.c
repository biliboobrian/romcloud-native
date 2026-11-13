// RomCloud Native : petites bibliothèques système des jeux 64 bits récents (Unity…). Le jeu tourne seul,
// hors ligne : registre sans effet durable, pas de certificats, pas de périphériques HID ni de réseau,
// pas d'OpenGL par WGL (le rendu passe par Direct3D), dossiers de l'utilisateur sous « C:\Users\Player ».
#include <stdio.h>
#include <stdlib.h>

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
    if (A0) wr64(A0, 0x7101);
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
    if (STK(7)) wr64(STK(7), 0x7201);
    if (STK(8)) wr32(STK(8), 1); // REG_CREATED_NEW_KEY
    RET(0);
}
static void a_RegCreateKeyW(RcnCtx *ctx) {
    if (A2) wr64(A2, 0x7201);
    RET(0);
}

static const RcnShim advapi32[] = {
    {"CryptAcquireContextW", a_CryptAcquireContextW, 0}, {"CryptCreateHash", s_Zero, 0},
    {"CryptDestroyHash", s_True, 0},                     {"CryptGenRandom", a_CryptGenRandom, 0},
    {"CryptGetHashParam", s_Zero, 0},                    {"CryptHashData", s_Zero, 0},
    {"CryptReleaseContext", s_True, 0},                  {"GetSidSubAuthority", s_Zero, 0},
    {"GetTokenInformation", s_Zero, 0},                  {"GetUserNameA", a_GetUserNameA, 0},
    {"GetUserNameW", a_GetUserNameW, 0},                 {"RegCreateKeyExW", a_RegCreateKeyExW, 0},
    {"RegCreateKeyW", a_RegCreateKeyW, 0},               {"RegDeleteValueA", s_NotFound, 0},
    {"RegSetValueExA", s_Zero, 0},                       {"RegSetValueExW", s_Zero, 0},
    {NULL, NULL, 0},
};

// --- crypt32, dbghelp, dwmapi, gdi32, opengl32 ---

static const RcnShim crypt32[] = {
    {"CertAddEncodedCertificateToStore", s_Zero, 0}, {"CertCloseStore", s_True, 0},
    {"CertFreeCertificateChain", s_Zero, 0},         {"CertFreeCertificateContext", s_True, 0},
    {"CertGetCertificateChain", s_Zero, 0},          {"CertOpenStore", s_Zero, 0},
    {"CertVerifyCertificateChainPolicy", s_Zero, 0}, {NULL, NULL, 0},
};
static const RcnShim dbghelp[] = {{"SymFromAddr", s_Zero, 0}, {"SymInitialize", s_Zero, 0}, {NULL, NULL, 0}};
static const RcnShim dwmapi[] = {{"DwmGetWindowAttribute", s_Fail, 0}, {"DwmSetWindowAttribute", s_Zero, 0}, {NULL, NULL, 0}};
static const RcnShim gdi32[] = {
    {"ChoosePixelFormat", s_Zero, 0}, {"SetPixelFormat", s_Zero, 0}, {"SwapBuffers", s_Zero, 0},
    {"DescribePixelFormat", s_Zero, 0}, {"GetDeviceCaps", s_Zero, 0}, {NULL, NULL, 0},
};
static const RcnShim opengl32[] = {
    {"wglCreateContext", s_Zero, 0},     {"wglDeleteContext", s_True, 0}, {"wglGetCurrentContext", s_Zero, 0},
    {"wglGetCurrentDC", s_Zero, 0},      {"wglGetProcAddress", s_Zero, 0}, {"wglMakeCurrent", s_Zero, 0},
    {NULL, NULL, 0},
};

// --- hid, setupapi : aucun périphérique ---

static void h_GetHidGuid(RcnCtx *ctx) { memset(PTR(A0), 0, 16); }
static const RcnShim hid[] = {
    {"HidD_FreePreparsedData", s_True, 0},   {"HidD_GetAttributes", s_Zero, 0},
    {"HidD_GetHidGuid", h_GetHidGuid, 0},    {"HidD_GetManufacturerString", s_Zero, 0},
    {"HidD_GetPreparsedData", s_Zero, 0},    {"HidD_GetProductString", s_Zero, 0},
    {"HidD_GetSerialNumberString", s_Zero, 0}, {"HidP_GetButtonCaps", s_Zero, 0},
    {"HidP_GetCaps", s_Zero, 0},             {"HidP_GetData", s_Zero, 0},
    {"HidP_GetValueCaps", s_Zero, 0},        {"HidP_MaxDataListLength", s_Zero, 0},
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
    {"ImmAssociateContext", s_Zero, 0}, {"ImmAssociateContextEx", s_True, 0}, {"ImmGetCompositionStringW", s_Zero, 0},
    {"ImmGetContext", s_Zero, 0},       {"ImmGetConversionStatus", s_Zero, 0}, {"ImmNotifyIME", s_Zero, 0},
    {"ImmReleaseContext", s_True, 0},   {"ImmSetCompositionStringW", s_Zero, 0}, {NULL, NULL, 0},
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
    {"WinHttpCloseHandle", s_True, 0}, {"WinHttpGetIEProxyConfigForCurrentUser", s_Zero, 0},
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
static void n_WaitFailed(RcnCtx *ctx) { RET(0xFFFFFFFFu); }
static const RcnShim ws2[] = {
    {"#11", n_inet_addr, 0},  {"#12", n_inet_ntoa, 0},   {"#51", s_Zero, 0},   {"#52", s_Zero, 0},
    {"#53", s_Zero, 0},       {"#57", n_gethostname, 0}, {"#103", s_Zero, 0},  {"#108", s_Zero, 0},
    {"#151", s_Zero, 0},      {"getnameinfo", n_getnameinfo, 0}, {"inet_ntop", s_Zero, 0},
    {"inet_pton", s_Zero, 0}, {"WSACloseEvent", s_True, 0}, {"WSACreateEvent", s_Zero, 0},
    {"WSAEnumNetworkEvents", s_MinusOne, 0}, {"WSAEventSelect", s_MinusOne, 0}, {"WSAPoll", s_MinusOne, 0},
    {"WSARecv", s_MinusOne, 0}, {"WSAResetEvent", s_True, 0}, {"WSASend", s_MinusOne, 0},
    {"WSASendDisconnect", s_MinusOne, 0}, {"WSASocketA", s_InvalidHandle, 0}, {"WSASocketW", s_InvalidHandle, 0},
    {"WSAWaitForMultipleEvents", n_WaitFailed, 0}, {NULL, NULL, 0},
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
    {"CoCreateFreeThreadedMarshaler", s_NotImpl, 0}, {"CoGetContextToken", s_NotImpl, 0},
    {"CoInitialize", s_Zero, 0},                     {"CoSetProxyBlanket", s_Zero, 0},
    {"PropVariantCopy", o_PropVariantCopy, 0},       {"StringFromGUID2", o_StringFromGUID2, 0},
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
    {"#2", o_SysAllocString, 0}, {"#4", o_SysAllocStringLen, 0}, {"#6", o_SysFreeString, 0},
    {"#7", o_SysStringLen, 0},   {"#12", s_NotImpl, 0},          {"#24", s_NotImpl, 0},
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
    u8 *block = rcn_galloc(ctx, 8 * (size_t)max_args + 2 * ((size_t)len + max_args), 1);
    if (!block) {
        RET(0);
        return;
    }
    u64 argv = (u64)(uintptr_t)block, out = argv + 8 * (u64)max_args;
    u32 argc = 0, i = 0;
    while (i < len) {
        while (i < len && (rd16(line + 2 * (u64)i) == ' ' || rd16(line + 2 * (u64)i) == '\t')) i++;
        if (i >= len) break;
        wr64(argv + 8 * (u64)argc++, out);
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
// SHGetFolderPathW(window, csidl, token, flags, path)
static void sh_SHGetFolderPathW(RcnCtx *ctx) {
    const char *path;
    switch ((u32)A1 & 0xFF) {
    case 0x1A: path = "C:\\Users\\Player\\AppData\\Roaming"; break; // CSIDL_APPDATA
    case 0x1C: path = "C:\\Users\\Player\\AppData\\Local"; break;   // CSIDL_LOCAL_APPDATA
    case 0x05: path = "C:\\Users\\Player\\Documents"; break;        // CSIDL_PERSONAL
    case 0x23: path = "C:\\ProgramData"; break;                     // CSIDL_COMMON_APPDATA
    case 0x28: path = "C:\\Users\\Player"; break;                   // CSIDL_PROFILE
    default: path = "C:\\Users\\Player"; break;
    }
    put_wide(STK(4), 260, path);
    RET(0);
}
static const RcnShim shell32[] = {
    {"CommandLineToArgvW", sh_CommandLineToArgvW, 0}, {"ShellExecuteW", sh_ShellExecuteW, 0},
    {"SHFileOperationW", s_True, 0},                  {"SHGetFolderPathW", sh_SHGetFolderPathW, 0},
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
static const RcnShim shlwapi[] = {{"PathCanonicalizeW", sh_PathCanonicalizeW, 0}, {"SHDeleteKeyW", s_NotFound, 0}, {NULL, NULL, 0}};
static const RcnShim version[] = {
    {"GetFileVersionInfoA", s_Zero, 0}, {"GetFileVersionInfoSizeA", s_Zero, 0}, {"VerQueryValueA", s_Zero, 0},
    {"GetFileVersionInfoW", s_Zero, 0}, {"GetFileVersionInfoSizeW", s_Zero, 0}, {"VerQueryValueW", s_Zero, 0},
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
static const RcnShim winmm[] = {{"waveInGetDevCapsA", m_NoDriver, 0}, {"waveOutGetDevCapsA", m_waveOutGetDevCapsA, 0}, {NULL, NULL, 0}};

// --- Recherche ---

static int starts_with(const char *text, const char *prefix) {
    for (; *prefix; text++, prefix++) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != *prefix) return 0;
    }
    return 1;
}

RcnFn rcn_sys_x64_lookup(const char *dll, const char *name) {
    static const struct {
        const char *dll;
        const RcnShim *table;
    } tables[] = {
        {"advapi32", advapi32}, {"crypt32", crypt32}, {"dbghelp", dbghelp},   {"dwmapi", dwmapi},
        {"gdi32", gdi32},       {"hid", hid},         {"imm32", imm32},       {"iphlpapi", iphlpapi},
        {"oleaut32", oleaut32}, {"ole32", ole32},     {"opengl32", opengl32}, {"setupapi", setupapi},
        {"shell32", shell32},   {"shlwapi", shlwapi}, {"version", version},   {"winhttp", winhttp},
        {"winmm", winmm},       {"ws2_32", ws2},
    };
    for (size_t i = 0; i < sizeof tables / sizeof tables[0]; i++) {
        if (!starts_with(dll, tables[i].dll)) continue;
        for (const RcnShim *s = tables[i].table; s->name; s++) {
            if (strcmp(s->name, name) == 0) return s->fn;
        }
    }
    return NULL;
}
