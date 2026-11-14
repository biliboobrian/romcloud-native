// Fonctions Windows qu'un module importe et que le runtime ne fournit pas encore. Lit sur l'entrée
// standard des lignes « DLL!fonction » (sortie de « rcn-recomp … --imports ») et écrit celles qui manquent.
//
//   rcn-recomp jeu.dll -o x.rcnx --imports | missing_imports
//
// Compilation : avec tout le runtime (sauf rcn_cli.c et rcn_linked.c), -DRCN_NATIVE.
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "rcn_internal.h"

int main(void) {
    char line[512];
    int missing = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        char *bang = strchr(line, '!');
        if (!bang) continue;
        *bang = 0;
        char dll[256];
        size_t i = 0;
        for (; line[i] && i + 1 < sizeof dll; i++) dll[i] = (char)tolower((unsigned char)line[i]);
        dll[i] = 0;
        u32 pop = 0;
        if (!rcn_win32_lookup(dll, bang + 1, 0, &pop)) {
            printf("%s!%s\n", line, bang + 1);
            missing++;
        }
    }
    fprintf(stderr, "%d manquantes\n", missing);
    return 0;
}
