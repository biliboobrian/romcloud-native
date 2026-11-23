// RomCloud Native : Winsock 1 (wsock32.dll) pour les jeux 32 bits, sans réseau.
// L'initialisation réussit et les conversions (ordre des octets, adresses) sont exactes ; toute socket est
// refusée (WSAENETDOWN) : un jeu solo continue sans réseau. Fonctions importées par leur numéro (« #115 »).
#include <stdio.h>
#include <stdlib.h>

#include "rcn_internal.h"

enum { WSAENETDOWN = 10050, SOCKET_ERROR32 = -1, INVALID_SOCKET32 = -1 };

static u32 last_error;
static void fail_net(RcnCtx *ctx) {
    last_error = WSAENETDOWN;
    RET_I32(SOCKET_ERROR32);
}
static u16 swap16(u16 v) { return (u16)(v >> 8 | v << 8); }
static u32 swap32(u32 v) { return v >> 24 | (v >> 8 & 0xFF00) | (v << 8 & 0xFF0000) | v << 24; }

// WSAStartup(version, &WSADATA) : WSADATA 32 bits (version, plus haute version, description, état…).
static void n_WSAStartup(RcnCtx *ctx) {
    if (A1) {
        memset(PTR(A1), 0, 400);
        wr16(A1, (u16)A0);
        wr16(A1 + 2, 0x0202);
        strcpy((char *)PTR(A1 + 4), "RomCloud Native (sans reseau)");
        strcpy((char *)PTR(A1 + 261), "Running");
    }
    RET(0);
}
static void n_WSACleanup(RcnCtx *ctx) { RET(0); }
static void n_WSAGetLastError(RcnCtx *ctx) { RET(last_error); }
static void n_socket(RcnCtx *ctx) {
    last_error = WSAENETDOWN;
    RET_I32(INVALID_SOCKET32);
}
static void n_fail(RcnCtx *ctx) { fail_net(ctx); }
static void n_htons(RcnCtx *ctx) { RET(swap16((u16)A0)); }
static void n_htonl(RcnCtx *ctx) { RET(swap32((u32)A0)); }
// inet_addr(« a.b.c.d ») : adresse dans l'ordre du réseau, INADDR_NONE si illisible.
static void n_inet_addr(RcnCtx *ctx) {
    unsigned a, b, c, d;
    const char *s = STR(A0);
    if (s && sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256)
        RET(a | b << 8 | c << 16 | d << 24);
    else
        RET(0xFFFFFFFFu);
}
// inet_ntoa(adresse) : texte dans la mémoire du jeu (même tampon à chaque appel, comme Windows).
static void n_inet_ntoa(RcnCtx *ctx) {
    static char *buf;
    if (!buf) buf = rcn_low_alloc(16, 1);
    u32 v = (u32)A0;
    if (buf) snprintf(buf, 16, "%u.%u.%u.%u", v & 0xFF, v >> 8 & 0xFF, v >> 16 & 0xFF, v >> 24);
    RET((uintptr_t)buf);
}
// gethostname(buffer, size)
static void n_gethostname(RcnCtx *ctx) {
    if ((u32)A1 < 10) {
        fail_net(ctx);
        return;
    }
    strcpy((char *)PTR(A0), "localhost");
    RET(0);
}
static void n_gethostbyname(RcnCtx *ctx) {
    last_error = 11001; // WSAHOST_NOT_FOUND
    RET(0);
}

static void n_set_last_error(RcnCtx *ctx) { last_error = (u32)A0; }
static void n_zero(RcnCtx *ctx) { RET(0); }
// getservbyname / getservbyport(nom ou port, protocole) : aucune base de services (WSANO_DATA).
static void n_getserv(RcnCtx *ctx) {
    last_error = 11004;
    RET(0);
}

// Numéros de wsock32.dll (ordre du .def de Windows).
const RcnShim rcn_wsock32[] = {
    {"#1", n_fail, S32(3)},   // accept
    {"#2", n_fail, S32(3)},   // bind
    {"#3", n_fail, S32(1)},   // closesocket
    {"#4", n_fail, S32(3)},   // connect
    {"#5", n_fail, S32(3)},   // getpeername
    {"#6", n_fail, S32(3)},   // getsockname
    {"#7", n_fail, S32(5)},   // getsockopt
    {"#8", n_htonl, S32(1)},  // htonl
    {"#9", n_htons, S32(1)},  // htons
    {"#10", n_fail, S32(3)},  // ioctlsocket
    {"#11", n_inet_addr, S32(1)},
    {"#12", n_inet_ntoa, S32(1)},
    {"#55", n_getserv, S32(2)}, // getservbyname
    {"#56", n_getserv, S32(2)}, // getservbyport
    {"getservbyname", n_getserv, S32(2)},
    {"getservbyport", n_getserv, S32(2)},
    {"#13", n_fail, S32(2)},  // listen
    {"#14", n_htonl, S32(1)}, // ntohl
    {"#15", n_htons, S32(1)}, // ntohs
    {"#16", n_fail, S32(4)},  // recv
    {"#17", n_fail, S32(6)},  // recvfrom
    {"#18", n_fail, S32(5)},  // select
    {"#19", n_fail, S32(4)},  // send
    {"#20", n_fail, S32(6)},  // sendto
    {"#21", n_fail, S32(5)},  // setsockopt
    {"#22", n_fail, S32(2)},  // shutdown
    {"#23", n_socket, S32(3)},
    {"#51", n_gethostbyname, S32(3)}, // gethostbyaddr : inconnu aussi
    {"#52", n_gethostbyname, S32(1)},
    {"#53", n_zero, S32(1)},          // getprotobyname : inconnu
    {"#57", n_gethostname, S32(2)},
    {"#103", n_zero, S32(5)},         // WSAAsyncGetHostByName
    {"#108", n_zero, S32(1)},         // WSACancelAsyncRequest
    {"#111", n_WSAGetLastError, S32(0)},
    {"#112", n_set_last_error, S32(1)}, // WSASetLastError
    {"#113", n_zero, S32(0)},           // WSACancelBlockingCall
    {"#114", n_zero, S32(0)},           // WSAIsBlocking
    {"#115", n_WSAStartup, S32(2)},
    {"#116", n_WSACleanup, S32(0)},
    {"#151", n_zero, S32(2)},         // __WSAFDIsSet
    // Mêmes fonctions par leur nom.
    {"WSAStartup", n_WSAStartup, S32(2)},
    {"WSACleanup", n_WSACleanup, S32(0)},
    {"WSAGetLastError", n_WSAGetLastError, S32(0)},
    {"socket", n_socket, S32(3)},
    {"closesocket", n_fail, S32(1)},
    {"htons", n_htons, S32(1)},
    {"htonl", n_htonl, S32(1)},
    {"ntohs", n_htons, S32(1)},
    {"ntohl", n_htonl, S32(1)},
    {"inet_addr", n_inet_addr, S32(1)},
    {"inet_ntoa", n_inet_ntoa, S32(1)},
    {"gethostname", n_gethostname, S32(2)},
    {"gethostbyname", n_gethostbyname, S32(1)},
};
const size_t rcn_wsock32_count = sizeof rcn_wsock32 / sizeof rcn_wsock32[0];
