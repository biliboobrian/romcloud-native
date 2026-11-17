// RomCloud Native : multimédia et réseau des jeux 64 bits (et de leurs DLL, FMOD par exemple).
//
// winmm : sortie de son waveOut vers celle de l'hôte (RcnHost.audio_open). Le jeu remet des tampons
// (WAVEHDR) ; le thread de son de l'hôte les lit au fil de l'eau, convertis en stéréo 16 bits, et les
// marque WHDR_DONE une fois joués (événement signalé si le jeu en a donné un). Pas d'entrée de son.
// ws2_32 : pas de réseau (WSAStartup réussit, toute socket échoue). msacm32 : aucun codec.
#include <stdlib.h>

#include "rcn_internal.h"

enum {
    MMSYSERR_NOERROR = 0,
    MMSYSERR_BADDEVICEID = 2,
    MMSYSERR_NODRIVER = 6,
    WAVERR_BADFORMAT = 32,
    WAVERR_STILLPLAYING = 33,
    WAVE_FORMAT_QUERY = 1,
    CALLBACK_TYPEMASK = 0x70000,
    CALLBACK_EVENT = 0x50000,
    WHDR_DONE = 1,
    WHDR_PREPARED = 2,
    WHDR_INQUEUE = 0x10,
    TIME_SAMPLES = 2,
    TIME_BYTES = 4,
    WAVE_HANDLE = 0x5701,
    QUEUE = 256,
};

static struct {
    pthread_mutex_t lock;
    RcnProcess *proc;
    int open;
    u32 channels, rate, bits, is_float, block;
    u64 queue[QUEUE]; // WAVEHDR en attente, dans l'ordre
    u32 head, count;
    u32 offset;       // octets déjà lus du premier tampon
    u64 played;       // octets joués depuis l'ouverture
    u32 callback_type;
    u64 callback;
    int host_state;   // 0 : pas encore ouverte, 1 : sortie de l'hôte, 2 : horloge seule (pas de son)
} wo = {.lock = PTHREAD_MUTEX_INITIALIZER};

static i32 read_sample(u64 at) {
    if (wo.is_float) {
        float f;
        memcpy(&f, PTR(at), 4);
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        return (i32)(f * 32767.0f);
    }
    switch (wo.bits) {
    case 8: return ((i32)rd8(at) - 128) << 8;
    case 16: return (i16)rd16(at);
    case 24: return (i32)((u32)rd8(at + 1) | (u32)rd8(at + 2) << 8) << 16 >> 16;
    default: return (i32)rd32(at) >> 16;
    }
}

// Thread de son de l'hôte : `frames` rames stéréo prises dans les tampons du jeu (silence s'il n'y en a pas).
static void fill(void *context, int16_t *out, int frames) {
    (void)context;
    pthread_mutex_lock(&wo.lock);
    u32 bytes_per_sample = wo.is_float ? 4 : wo.bits / 8;
    for (int f = 0; f < frames; f++) {
        if (!wo.open || !wo.count) {
            out[2 * f] = out[2 * f + 1] = 0;
            continue;
        }
        u64 hdr = wo.queue[wo.head];
        u64 data = rd64(hdr);
        u32 length = rd32(hdr + 8);
        if (wo.offset + wo.block <= length) {
            u64 frame = data + wo.offset;
            out[2 * f] = (int16_t)read_sample(frame);
            out[2 * f + 1] = (int16_t)read_sample(frame + (wo.channels > 1 ? bytes_per_sample : 0));
            wo.offset += wo.block;
            wo.played += wo.block;
        } else {
            out[2 * f] = out[2 * f + 1] = 0;
            wo.offset = length;
        }
        if (wo.offset >= length) {
            // Tampon joué : rendu au jeu.
            wr32(hdr + 24, (rd32(hdr + 24) & ~(u32)WHDR_INQUEUE) | WHDR_DONE);
            wo.head = (wo.head + 1) % QUEUE;
            wo.count--;
            wo.offset = 0;
            if (wo.callback_type == CALLBACK_EVENT && wo.callback) rcn_event_signal(wo.proc, wo.callback);
        }
    }
    pthread_mutex_unlock(&wo.lock);
}

// Sans sortie de son sur l'hôte : les tampons sont consommés au rythme du temps réel.
static void *clock_thread(void *arg) {
    (void)arg;
    rcn_thread_default_affinity();
    int16_t scratch[2 * 4800];
    for (;;) {
        rcn_sleep_ms(10);
        u32 frames = wo.rate / 100;
        if (frames > 4800) frames = 4800;
        fill(NULL, scratch, (int)frames);
    }
    return NULL;
}

static void start_output(RcnCtx *ctx) {
    if (wo.host_state) return;
    const RcnHost *host = ctx->proc->host;
    if (host->audio_open && host->audio_open(host->user, (int)wo.rate, fill, NULL) == 0) {
        wo.host_state = 1;
        return;
    }
    wo.host_state = 2;
    pthread_t t;
    pthread_create(&t, NULL, clock_thread, NULL);
    pthread_detach(t);
}

static void mm_timeGetTime(RcnCtx *ctx) { RET((u32)((rcn_now_ns() - ctx->proc->start_ns) / 1000000)); }
static void mm_One(RcnCtx *ctx) { RET(1); }
static void mm_Zero(RcnCtx *ctx) { RET(0); }
static void mm_NoDriver(RcnCtx *ctx) { RET(MMSYSERR_NODRIVER); }

// waveOutGetDevCapsW(device, &caps, size) : WAVEOUTCAPSW.
static void mm_waveOutGetDevCapsW(RcnCtx *ctx) {
    if ((u32)A0 != 0 && (u32)A0 != 0xFFFFFFFFu) {
        RET(MMSYSERR_BADDEVICEID);
        return;
    }
    u64 caps = A1;
    memset(PTR(caps), 0, 84 < (u32)A2 ? 84 : (u32)A2);
    static const char name[] = "RomCloud";
    for (int i = 0; name[i]; i++) wr16(caps + 8 + 2 * (u64)i, (u16)name[i]);
    wr32(caps + 72, 0xFFFFF); // tous les formats PCM usuels
    wr16(caps + 76, 2);
    RET(MMSYSERR_NOERROR);
}
// waveOutOpen(&handle, device, &format, callback, instance, flags)
static void mm_waveOutOpen(RcnCtx *ctx) {
    u64 format = A2;
    u32 flags = (u32)STK(5);
    u16 tag = rd16(format), channels = rd16(format + 2), bits = rd16(format + 14);
    u32 rate = rd32(format + 4), block = rd16(format + 12);
    int is_float = tag == 3;
    if (tag == 0xFFFE && rd16(format + 16) >= 22) is_float = rd32(format + 24) == 3;
    int ok = channels >= 1 && rate >= 8000 && rate <= 192000 && block > 0 &&
             (is_float ? bits == 32 : (bits == 8 || bits == 16 || bits == 24 || bits == 32)) &&
             (tag == 1 || tag == 3 || tag == 0xFFFE);
    if (!ok) {
        RET(WAVERR_BADFORMAT);
        return;
    }
    if (flags & WAVE_FORMAT_QUERY) {
        RET(MMSYSERR_NOERROR);
        return;
    }
    pthread_mutex_lock(&wo.lock);
    wo.proc = ctx->proc;
    wo.channels = channels;
    wo.rate = rate;
    wo.bits = bits;
    wo.is_float = is_float;
    wo.block = block;
    wo.head = wo.count = wo.offset = 0;
    wo.played = 0;
    wo.callback_type = flags & CALLBACK_TYPEMASK;
    wo.callback = A3;
    wo.open = 1;
    pthread_mutex_unlock(&wo.lock);
    if (A0) wr64(A0, WAVE_HANDLE);
    start_output(ctx);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutPrepareHeader(RcnCtx *ctx) {
    wr32(A1 + 24, (rd32(A1 + 24) | WHDR_PREPARED) & ~(u32)WHDR_DONE);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutUnprepareHeader(RcnCtx *ctx) {
    u32 flags = rd32(A1 + 24);
    if (flags & WHDR_INQUEUE) {
        RET(WAVERR_STILLPLAYING);
        return;
    }
    wr32(A1 + 24, flags & ~(u32)WHDR_PREPARED);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutWrite(RcnCtx *ctx) {
    pthread_mutex_lock(&wo.lock);
    if (wo.count < QUEUE) {
        wr32(A1 + 24, (rd32(A1 + 24) & ~(u32)WHDR_DONE) | WHDR_INQUEUE);
        wo.queue[(wo.head + wo.count++) % QUEUE] = A1;
    }
    pthread_mutex_unlock(&wo.lock);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutReset(RcnCtx *ctx) {
    pthread_mutex_lock(&wo.lock);
    while (wo.count) {
        u64 hdr = wo.queue[wo.head];
        wr32(hdr + 24, (rd32(hdr + 24) & ~(u32)WHDR_INQUEUE) | WHDR_DONE);
        wo.head = (wo.head + 1) % QUEUE;
        wo.count--;
    }
    wo.offset = 0;
    pthread_mutex_unlock(&wo.lock);
    if (wo.callback_type == CALLBACK_EVENT && wo.callback) rcn_event_signal(ctx->proc, wo.callback);
    RET(MMSYSERR_NOERROR);
}
static void mm_waveOutClose(RcnCtx *ctx) {
    mm_waveOutReset(ctx);
    wo.open = 0;
    RET(MMSYSERR_NOERROR);
}
// waveOutGetPosition(handle, &mmtime, size)
static void mm_waveOutGetPosition(RcnCtx *ctx) {
    u64 t = A1;
    pthread_mutex_lock(&wo.lock);
    u64 played = wo.played;
    pthread_mutex_unlock(&wo.lock);
    if (rd32(t) == TIME_SAMPLES && wo.block) {
        wr32(t + 4, (u32)(played / wo.block));
    } else {
        wr32(t, TIME_BYTES);
        wr32(t + 4, (u32)played);
    }
    RET(MMSYSERR_NOERROR);
}

const RcnShim rcn_winmm_x64[] = {
    {"timeBeginPeriod", mm_Zero, 0},
    {"timeEndPeriod", mm_Zero, 0},
    {"timeGetTime", mm_timeGetTime, 0},
    {"waveInAddBuffer", mm_NoDriver, 0},
    {"waveInClose", mm_NoDriver, 0},
    {"waveInGetDevCapsW", mm_NoDriver, 0},
    {"waveInGetNumDevs", mm_Zero, 0},
    {"waveInOpen", mm_NoDriver, 0},
    {"waveInPrepareHeader", mm_NoDriver, 0},
    {"waveInReset", mm_NoDriver, 0},
    {"waveInStart", mm_NoDriver, 0},
    {"waveInUnprepareHeader", mm_NoDriver, 0},
    {"waveOutClose", mm_waveOutClose, 0},
    {"waveOutGetDevCapsW", mm_waveOutGetDevCapsW, 0},
    {"waveOutGetNumDevs", mm_One, 0},
    {"waveOutGetPosition", mm_waveOutGetPosition, 0},
    {"waveOutOpen", mm_waveOutOpen, 0},
    {"waveOutPrepareHeader", mm_waveOutPrepareHeader, 0},
    {"waveOutReset", mm_waveOutReset, 0},
    {"waveOutUnprepareHeader", mm_waveOutUnprepareHeader, 0},
    {"waveOutWrite", mm_waveOutWrite, 0},
};
const size_t rcn_winmm_x64_count = sizeof rcn_winmm_x64 / sizeof rcn_winmm_x64[0];

// --- ws2_32 (par numéro d'ordre surtout) ---

// Réseau d'un PC hors ligne où rien n'écoute : les adresses numériques (et « localhost ») se résolvent,
// les sockets se créent, toute connexion est refusée (WSAECONNREFUSED), les noms d'hôtes sont inconnus.
// GOG Galaxy cherche son client local (127.0.0.1:9977) : sous Windows sans client, le refus immédiat
// lui fait conclure à l'absence du client et répondre au jeu ; un échec de résolution le faisait
// réessayer sans fin.
enum { WSAENETDOWN = 10050, WSAECONNREFUSED = 10061, WSAEOPNOTSUPP = 10045, WSAEINVAL = 10022, WSAHOST_NOT_FOUND = 11001 };
static void ws_Fail(RcnCtx *ctx) { // SOCKET_ERROR
    LAST_ERROR(ctx) = WSAENETDOWN;
    RET_I32(-1);
}
static void ws_Invalid(RcnCtx *ctx) { // INVALID_SOCKET
    LAST_ERROR(ctx) = WSAENETDOWN;
    RET(~0ull);
}
static void ws_LastError(RcnCtx *ctx) { RET(LAST_ERROR(ctx)); }
static void ws_SetLastError(RcnCtx *ctx) { LAST_ERROR(ctx) = (u32)A0; }
static void ws_Ok(RcnCtx *ctx) { RET(0); }
static void ws_socket(RcnCtx *ctx) {
    static u32 next;
    RET(0x5C000000ull + 4ull * __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED));
}
static void ws_connect(RcnCtx *ctx) {
    LAST_ERROR(ctx) = WSAECONNREFUSED;
    RET_I32(-1);
}
// getsockname(s, sockaddr, len) : adresse IPv4 locale quelconque.
static void ws_getsockname(RcnCtx *ctx) {
    if (A1 && A2 && rd32(A2) >= 16) {
        memset(PTR(A1), 0, 16);
        wr16(A1, 2);
        wr32(A2, 16);
    }
    RET(0);
}
// getsockopt(s, level, name, value, len) : 0.
static void ws_getsockopt(RcnCtx *ctx) {
    u64 value = STK(3), len = STK(4);
    if (value && len) memset(PTR(value), 0, rd32(len));
    RET(0);
}
// ConnectEx (obtenue par WSAIoctl) : refus immédiat ; boost.asio poste lui-même la complétion en erreur.
static void ws_ConnectEx(RcnCtx *ctx) {
    LAST_ERROR(ctx) = WSAECONNREFUSED;
    RET(0);
}
// WSAIoctl(s, code, in, in_len, out, out_len, returned, overlapped, routine) : seul ConnectEx est fourni.
static void ws_WSAIoctl(RcnCtx *ctx) {
    u64 in = A2, out = STK(4), out_len = STK(5), returned = STK(6);
    static const u8 connect_ex[16] = {0xb9, 0x07, 0xa2, 0x25, 0xf3, 0xdd, 0x60, 0x46,
                                      0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e};
    if ((u32)A1 == 0xC8000006u && in && (u32)A3 >= 16 && !memcmp(PTR(in), connect_ex, 16) && out && (u32)out_len >= 8) {
        static u64 thunk;
        if (!thunk) thunk = rcn_thunk(ctx, "mswsock", "ConnectEx", ws_ConnectEx, 0);
        wr64(out, thunk);
        if (returned) wr32(returned, 8);
        RET(0);
        return;
    }
    LAST_ERROR(ctx) = WSAEOPNOTSUPP;
    RET_I32(-1);
}
// Adresse IPv4 écrite en chiffres (ou « localhost ») : 1 et l'adresse (ordre du réseau), sinon 0.
static int ws_parse_ipv4(const char *s, u8 out[4]) {
    if (!strcmp(s, "localhost")) s = "127.0.0.1";
    unsigned a, b, c, d;
    char end;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &end) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0;
    out[0] = (u8)a, out[1] = (u8)b, out[2] = (u8)c, out[3] = (u8)d;
    return 1;
}
// Texte « a.b.c.d[:port] » d'une sockaddr_in, ou 0 (autre famille).
static int ws_ipv4_text(u64 sockaddr, char *out, size_t size, int with_port) {
    if (!sockaddr || rd16(sockaddr) != 2) return 0;
    const u8 *ip = PTR(sockaddr + 4);
    u16 port = rd16(sockaddr + 2);
    port = (u16)((port >> 8) | (port << 8));
    if (with_port && port) snprintf(out, size, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
    else snprintf(out, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return 1;
}
// WSAAddressToStringW(adresse, longueur, protocole, texte, longueur du texte en caractères).
static void ws_WSAAddressToStringW(RcnCtx *ctx) {
    char text[32];
    u64 out = A3, len = STK(4);
    if (!ws_ipv4_text(A0, text, sizeof text, 1) || !len) {
        LAST_ERROR(ctx) = WSAEINVAL;
        RET_I32(-1);
        return;
    }
    u32 need = (u32)strlen(text) + 1;
    if (!out || rd32(len) < need) {
        wr32(len, need);
        LAST_ERROR(ctx) = 10014; // WSAEFAULT
        RET_I32(-1);
        return;
    }
    for (u32 i = 0; i < need; i++) wr16(out + 2ull * i, (u8)text[i]);
    wr32(len, need);
    RET(0);
}
// WSAStringToAddressW(texte, famille, protocole, adresse, longueur) : IPv4 « a.b.c.d[:port] ».
static void ws_WSAStringToAddressW(RcnCtx *ctx) {
    char text[64];
    u64 in = A0, out = A3, len = STK(4);
    size_t n = 0;
    for (; in && n + 1 < sizeof text && rd16(in + 2 * n); n++) text[n] = (char)rd16(in + 2 * n);
    text[n] = 0;
    char *colon = strchr(text, ':');
    u32 port = 0;
    if (colon) *colon = 0, port = (u32)strtoul(colon + 1, NULL, 10);
    u8 ip[4];
    if ((u32)A1 != 2 || !ws_parse_ipv4(text, ip) || !out || !len || rd32(len) < 16) {
        LAST_ERROR(ctx) = WSAEINVAL;
        RET_I32(-1);
        return;
    }
    memset(PTR(out), 0, 16);
    wr16(out, 2);
    wr16(out + 2, (u16)((port >> 8) | (port << 8)));
    memcpy(PTR(out + 4), ip, 4);
    wr32(len, 16);
    RET(0);
}
// inet_ntop(famille, adresse, texte, taille) : IPv4 seulement.
static void ws_inet_ntop(RcnCtx *ctx) {
    if ((u32)A0 != 2 || !A1 || !A2) {
        RET(0);
        return;
    }
    const u8 *ip = PTR(A1);
    char text[16];
    int n = snprintf(text, sizeof text, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    if ((u64)n + 1 > A3) {
        RET(0);
        return;
    }
    memcpy(PTR(A2), text, (size_t)n + 1);
    RET(A2);
}
// inet_pton(famille, texte, adresse) : IPv4 seulement.
static void ws_inet_pton(RcnCtx *ctx) {
    u8 ip[4];
    if ((u32)A0 != 2) {
        LAST_ERROR(ctx) = 10047; // WSAEAFNOSUPPORT
        RET_I32(-1);
        return;
    }
    if (!A1 || !ws_parse_ipv4(STR(A1), ip) || !A2) {
        RET(0);
        return;
    }
    memcpy(PTR(A2), ip, 4);
    RET(1);
}
static void ws_Startup(RcnCtx *ctx) {
    if (A1) {
        memset(PTR(A1), 0, 408);
        wr16(A1, (u16)A0);
        wr16(A1 + 2, 0x0202);
    }
    RET(0);
}
static u32 swap32(u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }
static void ws_htonl(RcnCtx *ctx) { RET(swap32((u32)A0)); }
static void ws_htons(RcnCtx *ctx) { RET((u16)(((u16)A0 >> 8) | ((u16)A0 << 8))); }
// getaddrinfo(nœud, service, indications, résultat) : une adresse IPv4 pour un nœud numérique ou local
// (un seul bloc : addrinfo de 48 octets puis sockaddr_in), hôte inconnu sinon (hors ligne).
static void ws_getaddrinfo(RcnCtx *ctx) {
    u64 node = A0, service = A1, hints = A2, result = STK(3);
    if (result) wr64(result, 0);
    u8 ip[4] = {0, 0, 0, 0};
    int family = hints ? (int)rd32(hints + 4) : 0;
    int passive = hints && (rd32(hints) & 1); // AI_PASSIVE
    int known = node ? ws_parse_ipv4(STR(node), ip) : 1;
    if (!node && !passive) ip[0] = 127, ip[3] = 1;
    if (!known || (family != 0 && family != 2)) {
        static int told;
        if (told++ < 40) rcn_log("getaddrinfo(%s) : hors ligne", node ? STR(node) : "-");
        RET(WSAHOST_NOT_FOUND);
        return;
    }
    u32 port = service ? (u32)strtoul(STR(service), NULL, 10) : 0;
    u8 *block = calloc(1, 48 + 16);
    if (!block || !result) {
        free(block);
        RET(8); // WSA_NOT_ENOUGH_MEMORY
        return;
    }
    u64 info = (u64)(uintptr_t)block, addr = info + 48;
    wr32(info + 4, 2);                                // AF_INET
    wr32(info + 8, hints ? rd32(hints + 8) : 1);      // type demandé (SOCK_STREAM par défaut)
    wr32(info + 12, hints ? rd32(hints + 12) : 0);
    wr64(info + 16, 16);
    wr64(info + 32, addr);
    wr16(addr, 2);
    wr16(addr + 2, (u16)((port >> 8) | (port << 8)));
    memcpy(PTR(addr + 4), ip, 4);
    wr64(result, info);
    RET(0);
}
static void ws_freeaddrinfo(RcnCtx *ctx) {
    // Nos résultats : un bloc chacun, sans suivant.
    if (A0) free(PTR(A0));
}
static void ws_Nothing(RcnCtx *ctx) { (void)ctx; }
const RcnShim rcn_ws2_x64[] = {
    // 1 accept, 2 bind, 3 closesocket, 4 connect, 5 getpeername, 6 getsockname, 7 getsockopt, 10 ioctlsocket,
    // 13 listen, 16 recv, 17 recvfrom, 18 select, 19 send, 20 sendto, 21 setsockopt, 22 shutdown, 23 socket.
    {"#1", ws_Invalid, 0},  {"#2", ws_Ok, 0},       {"#3", mm_Zero, 0},    {"#4", ws_connect, 0},
    {"#5", ws_Fail, 0},     {"#6", ws_getsockname, 0}, {"#7", ws_getsockopt, 0}, {"#8", ws_htonl, 0},
    {"#9", ws_htons, 0},    {"#10", ws_Ok, 0},      {"#13", ws_Fail, 0},   {"#14", ws_htonl, 0},
    {"#15", ws_htons, 0},   {"#16", ws_Fail, 0},    {"#17", ws_Fail, 0},   {"#18", ws_Fail, 0},
    {"#19", ws_Fail, 0},    {"#20", ws_Fail, 0},    {"#21", ws_Ok, 0},     {"#22", ws_Ok, 0},
    {"#23", ws_socket, 0},  {"#111", ws_LastError, 0}, {"#112", ws_SetLastError, 0}, {"#115", ws_Startup, 0},
    {"#116", mm_Zero, 0},   {"WSAIoctl", ws_WSAIoctl, 0}, {"freeaddrinfo", ws_freeaddrinfo, 0}, {"getaddrinfo", ws_getaddrinfo, 0},
    {"WSAStartup", ws_Startup, 0}, {"WSACleanup", mm_Zero, 0}, {"WSAGetLastError", ws_LastError, 0},
    {"WSASetLastError", ws_SetLastError, 0}, {"WSASocketW", ws_socket, 0}, {"WSASocketA", ws_socket, 0},
    {"inet_pton", ws_inet_pton, 0}, {"inet_ntop", ws_inet_ntop, 0}, {"WSAAddressToStringW", ws_WSAAddressToStringW, 0},
    {"WSAStringToAddressW", ws_WSAStringToAddressW, 0}, {"WSARecv", ws_Fail, 0}, {"WSASend", ws_Fail, 0},
    {"socket", ws_socket, 0}, {"connect", ws_connect, 0}, {"closesocket", mm_Zero, 0},
};
const size_t rcn_ws2_x64_count = sizeof rcn_ws2_x64 / sizeof rcn_ws2_x64[0];

// --- msacm32 : aucun codec (ACMERR_NOTPOSSIBLE) ---

static void acm_NotPossible(RcnCtx *ctx) { RET(512); }
const RcnShim rcn_msacm_x64[] = {
    {"acmFormatSuggest", acm_NotPossible, 0},       {"acmStreamClose", acm_NotPossible, 0},
    {"acmStreamConvert", acm_NotPossible, 0},       {"acmStreamOpen", acm_NotPossible, 0},
    {"acmStreamPrepareHeader", acm_NotPossible, 0}, {"acmStreamSize", acm_NotPossible, 0},
    {"acmStreamUnprepareHeader", acm_NotPossible, 0},
};
const size_t rcn_msacm_x64_count = sizeof rcn_msacm_x64 / sizeof rcn_msacm_x64[0];
