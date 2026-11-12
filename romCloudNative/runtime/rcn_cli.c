// Lanceur en ligne de commande d'un jeu transposé (tests sur PC et sur l'appareil par adb) :
//   <programme> <exécutable d'origine>                 code traduit en C, lié au programme
//   <programme> <code.rcnx> <exécutable d'origine>     code machine traduit (compilé avec -DRCN_NATIVE)
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
    RcnResult result;
#if defined(RCN_NATIVE)
    // Code machine traduit : <programme> <code.rcnx> <exécutable d'origine>
    if (argc != 3) {
        fprintf(stderr, "usage : %s <code.rcnx> <executable d'origine>\n", argv[0]);
        return 2;
    }
    if (rcn_run_native(argv[1], argv[2], NULL, &result) != RCN_OK) {
#else
    if (argc != 2) {
        fprintf(stderr, "usage : %s <executable d'origine de %s>\n", argv[0], rcn_port_name());
        return 2;
    }
    if (rcn_port_run(argv[1], NULL, &result) != RCN_OK) {
#endif
        fflush(stdout);
        fprintf(stderr, "rcn : %s\n", result.error);
        return 125;
    }
    return (int)result.exit_code;
}
