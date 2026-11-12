cmake_minimum_required(VERSION 3.22.1)
project(rcnhost C)

# Runtime des jeux traduits (runtime/, sans le lanceur de test ni le point d'entrée du code en C) et pont
# avec l'application. Le code des jeux, traduit sur l'appareil par librcn_recomp.so, est chargé par
# rcn_run_native (rcn_native.c).
file(GLOB RUNTIME_SOURCES ../../../runtime/*.c)
list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "rcn_(cli|linked)\\.c$")
add_library(rcnhost SHARED rcn_jni.c ${RUNTIME_SOURCES})
target_include_directories(rcnhost PRIVATE ../../../runtime)
set_target_properties(rcnhost PROPERTIES C_VISIBILITY_PRESET hidden)
# Comme pour le code traduit : pas d'hypothèse sur les types des accès mémoire, pas de multiplication-
# addition fusionnée (résultats flottants du x86).
target_compile_options(rcnhost PRIVATE -O2 -fno-strict-aliasing -ffp-contract=off -w)
target_link_libraries(rcnhost dl android aaudio EGL GLESv3 m)