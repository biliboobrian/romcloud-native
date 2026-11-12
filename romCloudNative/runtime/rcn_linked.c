// Point d'entrée d'une bibliothèque de jeu produite par le générateur C de rcn-recomp : le code
// traduit (rcn_module) est lié au runtime. Inutile pour le code machine traduit (rcn_native.c).
#include "rcn_internal.h"

extern const RcnModule rcn_module;

const char *rcn_port_name(void) { return rcn_module.name; }

int rcn_port_run(const char *exe_path, const RcnHost *host, RcnResult *result) {
    return rcn_run_module(&rcn_module, exe_path, host, result);
}