// RomCloud Native : interface entre un jeu transposé (code traduit + runtime, une bibliothèque par
// jeu) et l'application qui l'héberge. Volontairement réduite : l'application charge la bibliothèque
// et appelle rcn_port_run ; le jeu lui parle par les fonctions de RcnHost.
#ifndef RCN_HOST_H
#define RCN_HOST_H

#include <stddef.h>
#include <stdint.h>

#define RCN_HOST_ABI 1

enum {
    RCN_STREAM_STDOUT = 1,
    RCN_STREAM_STDERR = 2,
};

typedef struct {
    uint32_t abi; // RCN_HOST_ABI
    void *user;
    // Texte écrit par le jeu sur sa sortie standard ou d'erreur.
    void (*write)(void *user, int stream, const char *data, size_t len);
} RcnHost;

enum {
    RCN_OK = 0,           // le jeu s'est terminé de lui-même (exit_code)
    RCN_ERR_LOAD = -1,    // exécutable illisible ou différent de celui qui a été traduit
    RCN_ERR_TRAP = -2,    // arrêt sur une instruction, un import ou une adresse non gérés
    RCN_ERR_HOST = -3,    // application hôte incompatible
};

typedef struct {
    int status;
    uint32_t exit_code;
    char error[512];
} RcnResult;

#if defined(_WIN32)
#define RCN_PORT_API
#else
#define RCN_PORT_API __attribute__((visibility("default")))
#endif

// Exécute le jeu jusqu'à sa fin. exe_path : exécutable Windows d'origine (ses données y sont lues).
RCN_PORT_API int rcn_port_run(const char *exe_path, const RcnHost *host, RcnResult *result);
// Nom de l'exécutable traduit.
RCN_PORT_API const char *rcn_port_name(void);

#endif
