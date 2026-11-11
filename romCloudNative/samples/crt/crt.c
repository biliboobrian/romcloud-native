// Deuxième programme de test : un programme C ordinaire, lié à la bibliothèque C de Windows (UCRT).
// Il exerce les fonctions que l'hôte doit fournir à la place de Windows : démarrage du CRT, printf et
// ses formats, chaînes, tas, fichiers, qsort (l'hôte rappelle une fonction du jeu), mathématiques,
// variables locales aux threads, arguments de la ligne de commande.
// Sa sortie doit être identique octet pour octet sous Windows et une fois traduit.
//
// Compilation (llvm-mingw) : x86_64-w64-mingw32-clang -O2 crt.c -o crt.exe
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

typedef struct {
    char name[16];
    int score;
    double ratio;
} Entry;

static int byScore(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    return x->score != y->score ? y->score - x->score : strcmp(x->name, y->name);
}
static int byName(const void *a, const void *b) { return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name); }

static _Thread_local int tlsCounter = 40;
static _Thread_local char tlsText[8] = "tls";

static int sum(int count, ...) {
    va_list args;
    va_start(args, count);
    int total = 0;
    for (int i = 0; i < count; i++) total += va_arg(args, int);
    va_end(args);
    return total;
}

static void atExit1(void) { puts("atexit 1"); }
static void atExit2(void) { puts("atexit 2"); }

static jmp_buf jump;
static void thrower(int depth) {
    if (depth == 0) longjmp(jump, 7);
    thrower(depth - 1);
}

int main(int argc, char **argv) {
    printf("argc = %d, argv[0] se termine par .exe : %s\n", argc, strstr(argv[0], ".exe") ? "oui" : "non");

    // printf : entiers, largeurs, précisions, chaînes, flottants.
    printf("[%d] [%5d] [%-5d] [%05d] [%+d] [%x] [%X] [%#x] [%o]\n", -42, 42, 42, 42, 42, 0xbeef, 0xbeef, 255, 8);
    printf("[%u] [%lld] [%llu] [%llx] [%I64d] [%zu]\n", 4000000000u, -9000000000000000000ll, 18000000000000000000ull,
           0xfedcba9876543210ull, (long long)1 << 40, sizeof(Entry));
    printf("[%s] [%10s] [%-10s] [%.3s] [%c] [%%] [%p]\n", "abc", "droite", "gauche", "tronque", 'Z', (void *)0x1234abcd);
    printf("[%f] [%.2f] [%10.3f] [%e] [%g] [%g] [%.0f]\n", 3.14159265, 2.71828, -1.5, 12345.678, 0.0001, 1e20, 2.5);
    printf("[%ls] [%hs] [%*d] [%.*f]\n", L"large", "etroit", 6, 7, 2, 1.23456);

    char buf[128];
    int n = snprintf(buf, sizeof buf, "%s-%d-%.1f", "snprintf", 12, 3.75);
    printf("%s (%d)\n", buf, n);
    n = snprintf(buf, 8, "%s", "depassement");
    printf("%s (%d)\n", buf, n);
    sprintf(buf, "%08.3f|%+.2e", 3.14159, 12345.6789);
    puts(buf);

    // Chaînes et caractères.
    char text[64] = "RomCloud";
    strcat(text, " Native");
    printf("%s : %zu caracteres, '%s', '%s'\n", text, strlen(text), strchr(text, 'C'), strrchr(text, 'a'));
    for (char *p = text; *p; p++) *p = (char)(isupper((unsigned char)*p) ? tolower(*p) : toupper(*p));
    printf("%s, cmp %d %d %d\n", text, strcmp("a", "b") < 0, strncmp("abcd", "abcf", 3), memcmp("abc", "abd", 3) < 0);
    printf("strtol %ld, strtoul %lu, atoi %d, strtod %.3f, atof %.2f\n", strtol("-1234xyz", NULL, 10),
           strtoul("ff", NULL, 16), atoi("  77"), strtod("2.5e3", NULL), atof("0.25"));
    int a, b;
    char word[16];
    printf("sscanf %d :", sscanf("12 34 mot", "%d %d %15s", &a, &b, word));
    printf(" %d %d %s\n", a, b, word);
    printf("wcslen %zu\n", wcslen(L"caracteres larges"));

    // Tas.
    Entry *entries = calloc(5, sizeof(Entry));
    static const char *names[] = {"delta", "alpha", "echo", "charlie", "bravo"};
    for (int i = 0; i < 5; i++) {
        strcpy(entries[i].name, names[i]);
        entries[i].score = (i * 37 + 11) % 4;
        entries[i].ratio = sqrt((double)(i + 1));
    }
    entries = realloc(entries, 6 * sizeof(Entry));
    strcpy(entries[5].name, "foxtrot");
    entries[5].score = 3;
    entries[5].ratio = 0.5;

    // qsort et bsearch : l'hôte rappelle une fonction du jeu.
    qsort(entries, 6, sizeof(Entry), byScore);
    for (int i = 0; i < 6; i++) printf("%d:%s:%.4f ", entries[i].score, entries[i].name, entries[i].ratio);
    putchar('\n');
    qsort(entries, 6, sizeof(Entry), byName);
    Entry key = {"echo", 0, 0};
    Entry *found = bsearch(&key, entries, 6, sizeof(Entry), byName);
    printf("bsearch : %s en position %d\n", found ? found->name : "?", found ? (int)(found - entries) : -1);
    free(entries);

    // Mathématiques (valeurs lues dans des variables volatiles : calculées à l'exécution).
    volatile double one = 1.0, half = 0.5, ten = 10.0, two = 2.0;
    volatile float twoF = 2.0f;
    printf("sin %.6f cos %.6f tan %.6f atan2 %.6f\n", sin(one), cos(one), tan(half), atan2(one, two));
    printf("exp %.6f log %.6f log10 %.6f pow %.6f\n", exp(one + half), log(ten), log10(ten * ten * ten), pow(two, half));
    printf("floor %.1f ceil %.1f fabs %.1f fmod %.3f sqrtf %.4f\n", floor(-two - half), ceil(-two - half), fabs(-two - one),
           fmod(ten, two + one + 0.3), (double)sqrtf(twoF));

    // Fichiers.
    const char *path = "rcn_test_fichier.tmp";
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("fopen : erreur %d\n", errno);
        return 1;
    }
    fprintf(f, "ligne %d\n", 1);
    fputs("ligne 2\n", f);
    unsigned char bytes[256];
    for (int i = 0; i < 256; i++) bytes[i] = (unsigned char)i;
    printf("fwrite %zu, ", fwrite(bytes, 1, sizeof bytes, f));
    printf("ftell %ld\n", ftell(f));
    fclose(f);
    f = fopen(path, "rb");
    char line[32];
    while (fgets(line, sizeof line, f) && line[0] == 'l') printf("lu : %s", line);
    fseek(f, -4, SEEK_END);
    unsigned char tail[4];
    size_t got = fread(tail, 1, 4, f);
    printf("fin du fichier : %zu octets %u %u %u %u, feof %d\n", got, tail[0], tail[1], tail[2], tail[3], fgetc(f) == EOF);
    fclose(f);
    printf("remove %d, fopen d'un fichier absent : %s\n", remove(path), fopen(path, "rb") ? "ouvert" : "refuse");

    // Variables locales au thread, arguments variables, saut non local.
    tlsCounter += 2;
    printf("tls %d %s, sum %d\n", tlsCounter, tlsText, sum(5, 1, 2, 3, 4, 5));
    int jumped = setjmp(jump);
    if (jumped == 0) thrower(5);
    printf("longjmp %d\n", jumped);

    time_t now = time(NULL);
    printf("time %s, clock %s\n", now > 1700000000 ? "plausible" : "faux", clock() >= 0 ? "ok" : "faux");
    const char *missing = getenv("RCN_VARIABLE_ABSENTE");
    printf("getenv %s\n", missing ? missing : "(absente)");

    atexit(atExit1);
    atexit(atExit2);
    fprintf(stderr, "message sur la sortie d'erreur\n");
    fflush(stdout);
    exit(7);
}
