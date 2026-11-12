cmake_minimum_required(VERSION 3.22.1)
project(rcnhost C)

# Pont entre l'application et un jeu transposé : charge sa bibliothèque et lui fournit l'hôte.
add_library(rcnhost SHARED rcn_jni.c)
target_include_directories(rcnhost PRIVATE ../../../runtime)
set_target_properties(rcnhost PROPERTIES C_VISIBILITY_PRESET hidden)
target_compile_options(rcnhost PRIVATE -Wall -Wextra)
target_link_libraries(rcnhost dl android)
