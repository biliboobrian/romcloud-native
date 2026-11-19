// RomCloud Native : printf et scanf de la bibliothèque C de Windows.
// Les arguments viennent d'un va_list du jeu (cases de 8 octets) et suivent les tailles de Windows :
// « long » fait 32 bits, « %ls » désigne de l'UTF-16, « %I64d » un entier de 64 bits. Chaque conversion
// est donc décodée ici, puis confiée au snprintf de l'hôte avec un format et un type sans ambiguïté.
#include <stdio.h>
#include <stdlib.h>

#include "rcn_internal.h"

static void reserve(RcnBuf *buf, size_t extra) {
    if (buf->len + extra + 1 <= buf->cap) return;
    size_t cap = buf->cap ? buf->cap * 2 : 256;
    while (cap < buf->len + extra + 1) cap *= 2;
    char *data = realloc(buf->data, cap);
    if (!data) abort();
    buf->data = data;
    buf->cap = cap;
}

void rcn_buf_put(RcnBuf *buf, const char *data, size_t len) {
    reserve(buf, len);
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = 0;
}

char *rcn_utf16_to_utf8(const u16 *text, i64 max) {
    size_t n = 0;
    while ((max < 0 || (i64)n < max) && text[n]) n++;
    char *out = malloc(n * 3 + 1); // au plus 3 octets par unité UTF-16
    if (!out) return NULL;
    char *p = out;
    for (size_t i = 0; i < n; i++) {
        u32 c = text[i];
        // Paire de substitution : un caractère au-delà de U+FFFF.
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
        }
        if (c < 0x80) {
            *p++ = (char)c;
        } else if (c < 0x800) {
            *p++ = (char)(0xC0 | c >> 6);
            *p++ = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            *p++ = (char)(0xE0 | c >> 12);
            *p++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *p++ = (char)(0x80 | (c & 0x3F));
        } else {
            *p++ = (char)(0xF0 | c >> 18);
            *p++ = (char)(0x80 | ((c >> 12) & 0x3F));
            *p++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *p++ = (char)(0x80 | (c & 0x3F));
        }
    }
    *p = 0;
    return out;
}

size_t rcn_utf8_to_utf16(const char *text, size_t len, u16 *out) {
    const u8 *s = (const u8 *)text;
    size_t n = 0;
    for (size_t i = 0; i < len;) {
        u32 c = s[i++];
        // Octets de suite attendus d'après le premier ; une séquence invalide donne U+FFFD.
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (c >= 0x80) {
            if (extra == 0 || i + extra > len) {
                c = 0xFFFD;
            } else {
                c &= 0x3F >> extra;
                for (int k = 0; k < extra; k++) c = (c << 6) | (s[i++] & 0x3F);
            }
        }
        if (c >= 0x10000) {
            c -= 0x10000;
            if (out) {
                out[n] = (u16)(0xD800 + (c >> 10));
                out[n + 1] = (u16)(0xDC00 + (c & 0x3FF));
            }
            n += 2;
        } else {
            if (out) out[n] = (u16)c;
            n++;
        }
    }
    return n;
}

// Taille d'un argument entier d'après son modificateur de longueur.
// LEN_PTR : « I », « z », « t » (taille d'un pointeur : 64 bits, ou 32 pour un jeu 32 bits).
typedef enum { LEN_DEFAULT, LEN_HH, LEN_H, LEN_L, LEN_64, LEN_WIDE, LEN_PTR } Length;

// Lit le modificateur de longueur en *p et avance.
static Length parse_length(const char **p) {
    const char *s = *p;
    Length len = LEN_DEFAULT;
    if (s[0] == 'h' && s[1] == 'h') {
        len = LEN_HH;
        s += 2;
    } else if (s[0] == 'h') {
        len = LEN_H;
        s += 1;
    } else if (s[0] == 'l' && s[1] == 'l') {
        len = LEN_64;
        s += 2;
    } else if (s[0] == 'l') {
        len = LEN_L;
        s += 1;
    } else if (s[0] == 'I' && s[1] == '6' && s[2] == '4') {
        len = LEN_64;
        s += 3;
    } else if (s[0] == 'I' && s[1] == '3' && s[2] == '2') {
        s += 3;
    } else if (s[0] == 'j') {
        len = LEN_64;
        s += 1;
    } else if (s[0] == 'I' || s[0] == 'z' || s[0] == 't') {
        len = LEN_PTR;
        s += 1;
    } else if (s[0] == 'w') {
        len = LEN_WIDE;
        s += 1;
    } else if (s[0] == 'L') {
        s += 1;
    }
    *p = s;
    return len;
}

// Ajoute le résultat d'un snprintf de l'hôte.
#define EMIT(out, spec, ...)                                                       \
    do {                                                                           \
        int need_ = snprintf(NULL, 0, spec, __VA_ARGS__);                          \
        if (need_ > 0) {                                                           \
            reserve(out, (size_t)need_);                                           \
            snprintf((out)->data + (out)->len, (size_t)need_ + 1, spec, __VA_ARGS__); \
            (out)->len += (size_t)need_;                                           \
        }                                                                          \
    } while (0)

// Case suivante du va_list du jeu : 8 octets en 64 bits ; en 32 bits, 4 (8 pour un entier de 64 bits).
static u64 next_arg(u64 *va, int bits32, int wide64) {
    u64 v;
    if (bits32 && !wide64) {
        v = rd32(*va);
        *va += 4;
    } else {
        v = rd64(*va);
        *va += 8;
    }
    return v;
}

static void format_impl(RcnBuf *out, const char *fmt, u64 va, int bits32);
void rcn_format(RcnBuf *out, const char *fmt, u64 va) { format_impl(out, fmt, va, 0); }
// Jeu 32 bits : cases de 4 octets (les double et les entiers de 64 bits en prennent 8).
void rcn_format32(RcnBuf *out, const char *fmt, u64 va) { format_impl(out, fmt, va, 1); }

static void format_impl(RcnBuf *out, const char *fmt, u64 va, int bits32) {
    reserve(out, 0);
    out->data[out->len] = 0;
    while (*fmt) {
        const char *percent = strchr(fmt, '%');
        if (!percent) {
            rcn_buf_put(out, fmt, strlen(fmt));
            break;
        }
        rcn_buf_put(out, fmt, (size_t)(percent - fmt));
        const char *p = percent + 1;
        // Format de l'hôte reconstruit : % drapeaux largeur .précision [ll] conversion.
        char spec[48] = "%";
        size_t n = 1;
        while (*p && strchr("-+ #0", *p)) {
            if (n < 8) spec[n++] = *p;
            p++;
        }
        int width = -1, precision = -1;
        if (*p == '*') {
            width = (i32)next_arg(&va, bits32, 0);
            p++;
            if (width < 0) { // largeur négative : alignement à gauche
                spec[n++] = '-';
                width = -width;
            }
        } else if (*p >= '0' && *p <= '9') {
            width = 0;
            while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        }
        if (*p == '.') {
            p++;
            precision = 0;
            if (*p == '*') {
                precision = (i32)next_arg(&va, bits32, 0);
                p++;
            } else {
                while (*p >= '0' && *p <= '9') precision = precision * 10 + (*p++ - '0');
            }
        }
        if (width > 100000) width = 100000;
        if (precision > 100000) precision = 100000;
        if (width >= 0) n += (size_t)snprintf(spec + n, 12, "%d", width);
        if (precision >= 0) n += (size_t)snprintf(spec + n, 13, ".%d", precision);
        Length len = parse_length(&p);
        if (len == LEN_PTR) len = bits32 ? LEN_DEFAULT : LEN_64;
        char conv = *p;
        if (!conv) break;
        fmt = p + 1;
        switch (conv) {
        case 'd':
        case 'i': {
            u64 raw = next_arg(&va, bits32, len == LEN_64);
            long long v = len == LEN_64 ? (i64)raw : len == LEN_H ? (i16)raw : len == LEN_HH ? (i8)raw : (i32)raw;
            snprintf(spec + n, 4, "lld");
            EMIT(out, spec, v);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            u64 raw = next_arg(&va, bits32, len == LEN_64);
            unsigned long long v = len == LEN_64 ? raw : len == LEN_H ? (u16)raw : len == LEN_HH ? (u8)raw : (u32)raw;
            snprintf(spec + n, 4, "ll%c", conv);
            EMIT(out, spec, v);
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            double v = rdf64(va);
            va += 8;
            snprintf(spec + n, 2, "%c", conv);
            EMIT(out, spec, v);
            break;
        }
        case 'c':
        case 'C': {
            u64 raw = next_arg(&va, bits32, 0);
            spec[n] = 's';
            spec[n + 1] = 0;
            // Un caractère large (« %lc », « %C ») devient sa suite UTF-8.
            if (len == LEN_L || len == LEN_WIDE || conv == 'C') {
                u16 wide[2] = {(u16)raw, 0};
                char *text = rcn_utf16_to_utf8(wide, 1);
                if (text) EMIT(out, spec, text);
                free(text);
            } else {
                char text[2] = {(char)raw, 0};
                // snprintf s'arrêterait sur un caractère nul : il est ajouté tel quel.
                if (text[0]) EMIT(out, spec, text);
                else rcn_buf_put(out, text, 1);
            }
            break;
        }
        case 's':
        case 'S': {
            u64 raw = next_arg(&va, bits32, 0);
            spec[n] = 's';
            spec[n + 1] = 0;
            int wide = (len == LEN_L || len == LEN_WIDE || conv == 'S') && len != LEN_H;
            if (!raw) {
                EMIT(out, spec, "(null)");
            } else if (wide) {
                char *text = rcn_utf16_to_utf8(PTR(raw), precision);
                if (text) EMIT(out, spec, text);
                free(text);
            } else {
                EMIT(out, spec, STR(raw));
            }
            break;
        }
        case 'p': {
            // Windows : 16 chiffres hexadécimaux en majuscules, sans « 0x ».
            unsigned long long v = next_arg(&va, bits32, 0);
            if (bits32) EMIT(out, "%08llX", v);
            else EMIT(out, "%016llX", v);
            break;
        }
        case 'n':
            wr32(next_arg(&va, bits32, 0), (u32)out->len);
            break;
        case '%':
            rcn_buf_put(out, "%", 1);
            break;
        default:
            // Conversion inconnue : recopiée telle quelle.
            rcn_buf_put(out, percent, (size_t)(fmt - percent));
            break;
        }
    }
}

char *rcn_scan_format(const char *fmt) {
    // Au plus un caractère ajouté par conversion (« I64 » → « ll » raccourcit).
    char *out = malloc(strlen(fmt) * 2 + 1);
    if (!out) return NULL;
    char *o = out;
    while (*fmt) {
        if (*fmt != '%') {
            *o++ = *fmt++;
            continue;
        }
        *o++ = *fmt++;
        if (*fmt == '%') {
            *o++ = *fmt++;
            continue;
        }
        while (*fmt == '*' || (*fmt >= '0' && *fmt <= '9')) *o++ = *fmt++;
        Length len = parse_length(&fmt);
        char conv = *fmt;
        if (!conv) break;
        if (strchr("diuxXon", conv)) {
            // « long » de Windows : 32 bits, comme « int ».
            if (len == LEN_HH) *o++ = 'h';
            if (len == LEN_HH || len == LEN_H) *o++ = 'h';
            if (len == LEN_64) {
                *o++ = 'l';
                *o++ = 'l';
            }
        } else if (strchr("eEfFgGaA", conv)) {
            // « %lf » : double ; « %Lf » : le long double de Windows est un double.
            if (len != LEN_DEFAULT) *o++ = 'l';
        } else if (strchr("sc[", conv)) {
            // Chaînes larges (UTF-16 chez le jeu) : non gérées.
            if (len == LEN_L || len == LEN_WIDE) {
                free(out);
                return NULL;
            }
            if (conv == '[') {
                // Ensemble de caractères recopié jusqu'à son « ] » (qui peut en être le premier membre).
                *o++ = *fmt++;
                if (*fmt == '^') *o++ = *fmt++;
                if (*fmt == ']') *o++ = *fmt++;
                while (*fmt && *fmt != ']') *o++ = *fmt++;
                if (!*fmt) break;
            }
        } else if (conv != 'p') {
            free(out);
            return NULL;
        }
        *o++ = *fmt++;
    }
    *o = 0;
    return out;
}
