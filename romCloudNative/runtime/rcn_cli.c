// Lanceur en ligne de commande d'un jeu transposé (tests sur PC et sur l'appareil par adb) :
//   <programme> <exécutable d'origine>
// Le code de sortie est celui du jeu, ou 125 si le runtime s'est arrêté sur une erreur.
#include <stdio.h>

#include "rcn_host.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    // Sortie du jeu transmise telle quelle (pas de conversion des fins de ligne).
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc != 2) {
        fprintf(stderr, "usage : %s <executable d'origine de %s>\n", argv[0], rcn_port_name());
        return 2;
    }
    RcnResult result;
    if (rcn_port_run(argv[1], NULL, &result) != RCN_OK) {
        fflush(stdout);
        fprintf(stderr, "rcn : %s\n", result.error);
        return 125;
    }
    return (int)result.exit_code;
}
