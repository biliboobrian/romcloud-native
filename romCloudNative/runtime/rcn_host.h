// RomCloud Native : interface entre un jeu transposé (code traduit + runtime, une bibliothèque par
// jeu) et l'application qui l'héberge. Volontairement réduite : l'application charge la bibliothèque
// et appelle rcn_port_run ; le jeu lui parle par les fonctions de RcnHost.
#ifndef RCN_HOST_H
#define RCN_HOST_H

#include <stddef.h>
#include <stdint.h>

#define RCN_HOST_ABI 5

enum {
    RCN_STREAM_STDOUT = 1,
    RCN_STREAM_STDERR = 2,
};

// Fonction du jeu qui remplit rames échantillons stéréo de 16 bits (gauche, droite, …), appelée par
// l'hôte depuis son thread de son.
typedef void (*RcnAudioFill)(void *context, int16_t *samples, int frames);

typedef struct {
    uint32_t abi; // RCN_HOST_ABI
    void *user;
    // Texte écrit par le jeu sur sa sortie standard ou d'erreur.
    void (*write)(void *user, int stream, const char *data, size_t len);
    // Image du jeu à afficher (lignes de `pitch` octets ; format : RCN_PIXEL_*). Peut être NULL.
    // pixels NULL : seule la taille de l'image est annoncée (elle est dessinée par le GPU dans la fenêtre).
    void (*present)(void *user, const void *pixels, int width, int height, int pitch, int format);
    // État de la manette : bits RCN_PAD_*. Peut être NULL.
    uint32_t (*pad)(void *user);
    // Ouvre la sortie de son à sample_rate Hz, stéréo : l'hôte appelle ensuite ill chaque fois qu'il
    // lui faut des échantillons. Renvoie 0 en cas de succès. Peut être NULL (pas de son).
    int (*audio_open)(void *user, int sample_rate, RcnAudioFill fill, void *context);
    // Fenêtre où le jeu peut dessiner par le GPU (Android : ANativeWindow *, avec une référence que
    // l'appelant rend), ou NULL si elle n'existe pas pour l'instant. *generation change quand la fenêtre
    // est remplacée. Peut être NULL : rendu logiciel remis à `present`.
    void *(*window)(void *user, uint32_t *generation);
    // Axes analogiques de la manette : stick gauche x, y, stick droit x, y (-32768 … 32767, y vers le bas),
    // gâchettes gauche et droite (0 … 32767). Peut être NULL (manette tout-ou-rien).
    void (*pad_axes)(void *user, int16_t axes[6]);
} RcnHost;

enum {
    RCN_PIXEL_RGB565 = 1,   // 16 bits : rouge 5, vert 6, bleu 5
    RCN_PIXEL_XRGB8888 = 2, // 32 bits : octets bleu, vert, rouge, inutilisé
};

enum {
    RCN_PAD_UP = 1 << 0,
    RCN_PAD_DOWN = 1 << 1,
    RCN_PAD_LEFT = 1 << 2,
    RCN_PAD_RIGHT = 1 << 3,
    RCN_PAD_A = 1 << 4, // bouton du bas
    RCN_PAD_B = 1 << 5, // bouton de droite
    RCN_PAD_X = 1 << 6,
    RCN_PAD_Y = 1 << 7,
    RCN_PAD_START = 1 << 8,
    RCN_PAD_SELECT = 1 << 9,
    RCN_PAD_L = 1 << 10,
    RCN_PAD_R = 1 << 11,
    RCN_PAD_L3 = 1 << 12, // clic du stick gauche
    RCN_PAD_R3 = 1 << 13, // clic du stick droit
    RCN_PAD_LT = 1 << 14, // gâchettes, en tout-ou-rien
    RCN_PAD_RT = 1 << 15,
    RCN_PAD_QUIT = 1 << 30, // l'utilisateur demande à quitter le jeu
};

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

// Code machine traduit sur l'appareil (fichier .rcnx de rcn-recomp) : le runtime le charge et l'exécute.
// Fourni par la bibliothèque du runtime, qui ne contient aucun jeu.
RCN_PORT_API int rcn_run_native(const char *code_path, const char *exe_path, const RcnHost *host, RcnResult *result);

#endif
