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
    for (u32 i = 0; i < count; i++) handles[i] = rd64(A1 + 8ull * i);
    RET(rcn_wait_handles(ctx, count, handles, (u32)A2 != 0, (u32)A3));
}
static const RcnShim ws2[] = {
    {"#11", n_inet_addr, 0},  {"#12", n_inet_ntoa, 0},   {"#51", s_Zero, 0},   {"#52", s_Zero, 0},
    {"#53", s_Zero, 0},       {"#57", n_gethostname, 0}, {"#103", s_Zero, 0},  {"#108", s_Zero, 0},
    {"#151", s_Zero, 0},      {"getnameinfo", n_getnameinfo, 0}, {"inet_ntop", s_Zero, 0},
    {"inet_pton", s_Zero, 0}, {"WSACloseEvent", n_WSACloseEvent, 0}, {"WSACreateEvent", n_WSACreateEvent, 0},
    {"WSAEnumNetworkEvents", s_MinusOne, 0}, {"WSAEventSelect", s_MinusOne, 0}, {"WSAPoll", s_MinusOne, 0},
    {"WSARecv", s_MinusOne, 0}, {"WSAResetEvent", n_WSAResetEvent, 0}, {"WSASetEvent", n_WSASetEvent, 0}, {"WSASend", s_MinusOne, 0},
    {"WSASendDisconnect", s_MinusOne, 0}, {"WSASocketA", s_InvalidHandle, 0}, {"WSASocketW", s_InvalidHandle, 0},
    {"WSAWaitForMultipleEvents", n_WSAWaitForMultipleEvents, 0}, {NULL, NULL, 0},
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
    {"CryptAcquireContextA", a_CryptAcquireContextW, 0},
    {"OpenSCManagerA", s_AccessDenied, 0}, {"OpenServiceA", s_AccessDenied, 0}, {"CloseServiceHandle", s_True, 0},
    {"ControlService", s_AccessDenied, 0}, {"QueryServiceStatus", s_AccessDenied, 0}, {"StartServiceA", s_AccessDenied, 0},
    {"RegisterEventSourceW", s_EventSource, 0}, {"ReportEventW", s_True, 0}, {"DeregisterEventSource", s_True, 0},
    {NULL, NULL, 0},
};
static const RcnShim crypt32_more[] = {
    {"CertAddCertificateContextToStore", s_Zero, 0}, {"CertCreateCertificateChainEngine", s_Zero, 0},
    {"CertFindCertificateInStore", s_Zero, 0}, {"CertFreeCertificateChainEngine", s_Nothing, 0},
    {"CertGetNameStringA", s_Zero, 0}, {"CryptQueryObject", s_Zero, 0}, {"CryptStringToBinaryA", s_Zero, 0},
    {NULL, NULL, 0},
};
static const RcnShim gdi32_more[] = {
    {"CreateCompatibleBitmap", s_Zero, 0}, {"DeleteObject", s_True, 0}, {"GetDIBits", s_Zero, 0}, {"GetObjectW", s_Zero, 0},
    {"AddFontMemResourceEx", s_Zero, 0}, {"CreateFontIndirectW", s_Zero, 0}, {"RemoveFontMemResourceEx", s_True, 0},
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
static const RcnShim more_sys[] = {
    {"GetStockObject", s_True, 0},
    {"RegCreateKeyExA", a_RegCreateKeyExW, 0}, {"RegGetValueA", s_NotFound, 0}, {"RegGetValueW", s_NotFound, 0},
    {"SHGetFolderPathA", sh_SHGetFolderPathA, 0}, {"ShellExecuteA", sh_ShellExecuteW, 0},
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
static const RcnShim ws2_more[] = {{"WSAAddressToStringW", s_WsaInvalid, 0}, {"WSAStringToAddressW", s_WsaInvalid, 0}, {NULL, NULL, 0}};
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
static const RcnShim rpcrt4[] = {{"UuidCreate", s_UuidCreate, 0}, {NULL, NULL, 0}};

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

// VARIANT (24 octets) : type (u16) au début, valeur à 8.
static void v_VariantInit(RcnCtx *ctx) {
    if (A0) memset(PTR(A0), 0, 24);
}
static void v_VariantClear(RcnCtx *ctx) {
    if (A0) {
        if (rd16(A0) == 8 && rd64(A0 + 8)) rcn_gfree(ctx, PTR(rd64(A0 + 8) - 4)); // VT_BSTR
        memset(PTR(A0), 0, 24);
    }
    RET(0);
}
static void v_VariantCopy(RcnCtx *ctx) {
    if (!A0 || !A1) {
        RET(0x80070057u);
        return;
    }
    memcpy(PTR(A0), PTR(A1), 24);
    if (rd16(A1) == 8 && rd64(A1 + 8)) {
        u64 src = rd64(A1 + 8);
        wr64(A0 + 8, bstr(ctx, src, rd32(src - 4) / 2));
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
static void s_NoData(RcnCtx *ctx) { RET(232); }      // ERROR_NO_DATA
static const RcnShim ue_sys[] = {
    // oleaut32 : VariantInit, VariantClear, VariantCopy, SafeArrayPutElement, GetErrorInfo, SafeArrayCreateVector.
    {"#8", v_VariantInit, 0}, {"#9", v_VariantClear, 0}, {"#10", v_VariantCopy, 0}, {"#26", s_NotImpl, 0},
    {"#200", v_GetErrorInfo, 0}, {"#411", s_Zero, 0},
    // gdi32
    {"CreateCompatibleDC", s_True, 0}, {"DeleteDC", s_True, 0}, {"CreateDIBSection", s_Zero, 0},
    {"CreateBitmap", s_True, 0}, {"CreateFontW", s_True, 0}, {"CreatePen", s_True, 0},
    {"CreateSolidBrush", s_True, 0}, {"CreateRectRgn", s_True, 0}, {"CreateRoundRectRgn", s_True, 0},
    {"CombineRgn", s_Two, 0}, {"PtInRegion", s_Zero, 0}, {"CreateDCW", s_True, 0}, {"ExtTextOutW", s_True, 0},
    {"TextOutW", s_True, 0}, {"GetTextExtentPoint32W", g_GetTextExtentPoint32W, 0}, {"LineTo", s_True, 0},
    {"MoveToEx", s_True, 0}, {"Polygon", s_True, 0}, {"Rectangle", s_True, 0}, {"RoundRect", s_True, 0},
    {"SetBkColor", s_Zero, 0}, {"SetBkMode", s_True, 0}, {"SetTextAlign", s_Zero, 0}, {"SetTextColor", s_Zero, 0},
    {"GetICMProfileW", s_Zero, 0},
    // imm32
    {"ImmCreateContext", s_True, 0}, {"ImmDestroyContext", s_True, 0}, {"ImmGetDescriptionW", s_Zero, 0},
    {"ImmGetIMEFileNameW", s_Zero, 0}, {"ImmGetProperty", s_Zero, 0}, {"ImmSetCandidateWindow", s_True, 0},
    {"ImmSetCompositionWindow", s_True, 0},
    // iphlpapi
    {"GetAdaptersInfo", s_NoData, 0}, {"IcmpCloseHandle", s_True, 0},
    // setupapi
    {"CM_Get_Device_IDW", s_CrFailure, 0}, {"SetupDiGetClassDevsExW", s_InvalidHandle, 0},
    {"SetupDiGetDevicePropertyW", s_Zero, 0}, {"SetupDiGetDeviceRegistryPropertyW", s_Zero, 0},
    {"SetupDiOpenDevRegKey", s_InvalidHandle, 0},
    // shell32, uxtheme, UIAutomationCore
    {"ShellExecuteExW", s_Zero, 0}, {"SetWindowTheme", s_Zero, 0},
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
    {"CryptDestroyKey", s_True, 0}, {"CryptEnumProvidersW", s_Zero, 0}, {"CryptExportKey", s_Zero, 0},
    {"CryptGetProvParam", s_Zero, 0}, {"CryptGetUserKey", s_Zero, 0}, {"CryptSetHashParam", s_Zero, 0},
    {"CryptSignHashW", s_Zero, 0}, {"RegSetKeyValueW", s_Zero, 0},
    // crypt32 : magasins de certificats vides.
    {"CertDuplicateCertificateContext", s_Zero, 0}, {"CertEnumCertificatesInStore", s_Zero, 0},
    {"CertGetCertificateContextProperty", s_Zero, 0}, {"CertGetEnhancedKeyUsage", s_Zero, 0},
    {"CertGetIntendedKeyUsage", s_Zero, 0}, {"CertGetNameStringW", s_Zero, 0},
    {"CertOpenSystemStoreA", s_Zero, 0}, {"CertOpenSystemStoreW", s_Zero, 0},
    {NULL, NULL, 0},
};

extern const RcnShim rcn_d3dx9_math[], rcn_dinput8[], rcn_dsound_x64[], rcn_keyboard_user32[];
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
        {"advapi32", advapi32_more}, {"crypt32", crypt32_more}, {"gdi32", gdi32_more}, {"iphlpapi", iphlpapi_more},
        {"wininet", wininet},   {"ws2_32", ws2_more}, {"mswsock", mswsock}, {"bcrypt", bcrypt_more},
        {"setupapi", setupapi_more}, {"usp10", usp10}, {"rpcrt4", rpcrt4}, {"security", security}, {"secur32", security},
        {"gdi32", more_sys}, {"advapi32", more_sys}, {"shell32", more_sys}, {"hid", more_sys}, {"winhttp", more_sys},
        {"winmm", more_sys}, {"ws2_32", more_sys}, {"ole32", more_sys}, {"dinput8", more_sys},
        {"oleaut32", ue_sys}, {"gdi32", ue_sys}, {"imm32", ue_sys}, {"iphlpapi", ue_sys}, {"setupapi", ue_sys},
        {"shell32", ue_sys}, {"uxtheme", ue_sys}, {"uiautomationcore", ue_sys}, {"winhttp", ue_sys}, {"ws2_32", ue_sys},
        {"dwmapi", ue_sys}, {"ole32", ue_sys}, {"advapi32", ue_sys}, {"crypt32", ue_sys}, {"cfgmgr32", ue_sys},
        {"d3dx9", rcn_d3dx9_math}, {"dinput8", rcn_dinput8}, {"dsound", rcn_dsound_x64}, {"user32", rcn_keyboard_user32},
    };
    for (size_t i = 0; i < sizeof tables / sizeof tables[0]; i++) {
        if (!starts_with(dll, tables[i].dll)) continue;
        for (const RcnShim *s = tables[i].table; s->name; s++) {
            if (strcmp(s->name, name) == 0) return s->fn;
        }
    }
    return NULL;
}
