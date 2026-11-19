// Test dll32 : exécutable x86 32 bits sans bibliothèque C qui appelle une DLL du jeu relocalisée
// (dll32lib.dll, liée à la même adresse que lui). Sortie par WriteFile.
#include <windows.h>

__declspec(dllimport) int dll_op(int which, int a, int b);
__declspec(dllimport) const char *dll_name(int n);
__declspec(dllimport) int dll_square(int n);
__declspec(dllimport) int dll_switch(int n);
__declspec(dllimport) int dll_callback(int (*fn)(const char *), const char *text);

static HANDLE out;
static void put(const char *s) {
    DWORD n = 0, len = 0;
    while (s[len]) len++;
    WriteFile(out, s, len, &n, NULL);
}
static void put_int(int v) {
    char buf[16];
    int i = 15;
    int neg = v < 0;
    unsigned u = neg ? 0u - (unsigned)v : (unsigned)v;
    buf[i] = 0;
    do {
        buf[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg) buf[--i] = '-';
    put(buf + i);
}
static int text_length(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void start(void) {
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    put("op ");
    put_int(dll_op(0, 6, 7));
    put(" ");
    put_int(dll_op(1, 6, 7));
    put(" ");
    put_int(dll_op(2, 6, 7));
    put("\n");
    for (int i = 0; i < 6; i++) {
        put(dll_name(i));
        put(i < 5 ? " " : "\n");
    }
    put("carres ");
    for (int i = 0; i < 16; i += 5) {
        put_int(dll_square(i));
        put(" ");
    }
    put("\nswitch ");
    for (int i = -1; i < 9; i++) {
        put_int(dll_switch(i));
        put(" ");
    }
    put("\nrappel ");
    put_int(dll_callback(text_length, "relocalisee"));
    put("\n");
    ExitProcess(5);
}
