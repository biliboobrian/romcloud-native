// DLL x86 32 bits du test dll32 : liée à la même adresse que l'exécutable (0x400000), elle est forcément
// chargée ailleurs et relocalisée. Tout ce qui contient une adresse absolue de l'image est exercé :
// chaînes (push offset), tableau de pointeurs de fonctions, switch par table de sauts, tableau indexé
// ([reg*4 + table]), variable globale, rappel d'une fonction de l'exécutable.
#include <windows.h>

static const char *const names[] = {"zero", "un", "deux", "trois", "quatre", "cinq"};
static int squares[16];
static int calls;

static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
static int sub(int a, int b) { return a - b; }
typedef int (*Op)(int, int);
static Op ops[] = {add, mul, sub};

__declspec(dllexport) int dll_op(int which, int a, int b) {
    calls++;
    return ops[which % 3](a, b);
}

__declspec(dllexport) const char *dll_name(int n) { return n >= 0 && n < 6 ? names[n] : "?"; }

__declspec(dllexport) int dll_square(int n) { return squares[n & 15]; }

// Assez de cas contigus pour que le compilateur fasse une table de sauts.
__declspec(dllexport) int dll_switch(int n) {
    switch (n) {
    case 0: return 11;
    case 1: return 23;
    case 2: return 37;
    case 3: return 41;
    case 4: return 59;
    case 5: return 67;
    case 6: return 73;
    case 7: return 89;
    default: return -1;
    }
}

__declspec(dllexport) int dll_callback(int (*fn)(const char *), const char *text) { return fn(text) + calls; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)module, (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        for (int i = 0; i < 16; i++) squares[i] = i * i;
    return TRUE;
}
