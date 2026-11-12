// Exceptions C++ (llvm-mingw : déroulement SEH de Windows x64 — RaiseException, gestionnaires de cadre,
// RtlUnwindEx). Le programme traduit doit écrire exactement la même chose que l'original.
#include <cstdio>
#include <stdexcept>
#include <string>

static int destroyed = 0;

struct Guard {
    const char *name;
    explicit Guard(const char *n) : name(n) {}
    ~Guard() {
        destroyed++;
        std::printf("  ~Guard(%s)\n", name);
    }
};

struct Custom {
    int code;
};

__attribute__((noinline)) static int deep(int n) {
    Guard g("deep");
    if (n == 0) throw std::runtime_error("fond atteint");
    return deep(n - 1) + 1;
}

__attribute__((noinline)) static void throw_custom(int code) {
    Guard g("custom");
    throw Custom{code};
}

__attribute__((noinline)) static int rethrow() {
    try {
        throw_custom(7);
    } catch (Custom &c) {
        std::printf("  relance de %d\n", c.code);
        c.code *= 6;
        throw;
    }
    return 0;
}

__attribute__((noinline)) static int nested() {
    int total = 0;
    for (int i = 0; i < 3; i++) {
        try {
            try {
                if (i == 1) throw std::logic_error("interne");
                if (i == 2) throw 42;
                total += 1;
            } catch (std::logic_error &e) {
                std::printf("  interne : %s\n", e.what());
                total += 10;
                throw std::string("externe");
            }
        } catch (std::string &s) {
            std::printf("  attrape : %s\n", s.c_str());
            total += 100;
        } catch (int v) {
            std::printf("  entier : %d\n", v);
            total += v;
        }
    }
    return total;
}

int main() {
    std::printf("1. profondeur\n");
    try {
        deep(5);
    } catch (std::exception &e) {
        std::printf("  attrape : %s\n", e.what());
    }
    std::printf("2. relance\n");
    try {
        rethrow();
    } catch (Custom &c) {
        std::printf("  code final %d\n", c.code);
    }
    std::printf("3. imbrique : %d\n", nested());
    std::printf("4. boucle\n");
    int caught = 0;
    for (int i = 0; i < 1000; i++) {
        try {
            if (i % 3 == 0) throw i;
        } catch (int) {
            caught++;
        }
    }
    std::printf("  %d attrapes\n", caught);
    std::printf("destructeurs : %d\n", destroyed);
    return destroyed == 7 ? 0 : 1;
}
