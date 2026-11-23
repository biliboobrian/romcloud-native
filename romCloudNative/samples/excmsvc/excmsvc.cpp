// Exceptions C++ au format de Visual C++ (__CxxFrameHandler3, _CxxThrowException de vcruntime140.dll),
// sans bibliothèque C : la sortie passe par kernel32. Compilé par clang pour la cible MSVC (scripts/test.mjs).
extern "C" __declspec(dllimport) void *__stdcall GetStdHandle(unsigned long);
extern "C" __declspec(dllimport) int __stdcall WriteFile(void *, const void *, unsigned long, unsigned long *, void *);
extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned);

static void out(const char *s) {
    unsigned long n = 0, len = 0;
    while (s[len]) len++;
    WriteFile(GetStdHandle((unsigned long)-11), s, len, &n, 0);
}
static int dtors, copies;
struct Base {
    int code;
    Base(int c) : code(c) {}
    Base(const Base &o) : code(o.code) { copies++; }
    virtual ~Base() {}
};
struct Derived : Base {
    Derived(int c) : Base(c) {}
};
struct Guard {
    const char *name;
    Guard(const char *n) : name(n) {}
    ~Guard() {
        dtors++;
        out("~");
        out(name);
        out("\n");
    }
};
__declspec(noinline) void thrower(int k) {
    Guard g("thrower");
    if (k == 1) throw 42;
    if (k == 2) throw Derived(7);
    if (k == 3) throw "texte";
}
__declspec(noinline) int middle(int k) {
    Guard g("middle");
    thrower(k);
    return 0;
}
extern "C" void start() {
    try { middle(1); } catch (int v) { out(v == 42 ? "int 42\n" : "int ?\n"); }
    try { middle(2); } catch (Base &b) { out(b.code == 7 ? "Base& 7\n" : "Base& ?\n"); }
    try { middle(3); } catch (int) { out("faux\n"); } catch (const char *s) { out("char* "); out(s); out("\n"); }
    try {
        try { middle(1); } catch (int) { out("relance\n"); throw; }
    } catch (int v) { out(v == 42 ? "relancee 42\n" : "?\n"); }
    try { middle(2); } catch (...) { out("...\n"); }
    // Par valeur : constructeur de copie appelé pour l'objet du catch.
    copies = 0;
    try { middle(2); } catch (Base b) { out(b.code == 7 && copies > 0 ? "Base copie 7\n" : "Base copie ?\n"); }
    // Nouvelle exception lancée depuis un bloc catch, prise plus haut.
    try {
        try { middle(1); } catch (int) { Guard g("catch"); throw Derived(9); }
    } catch (Derived &d) { out(d.code == 9 ? "Derived 9 depuis catch\n" : "?\n"); }
    out(dtors == 15 ? "destructeurs ok\n" : "destructeurs faux\n");
    ExitProcess(0);
}
