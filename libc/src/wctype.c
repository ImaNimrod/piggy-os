#include <ctype.h>
#include <wctype.h>

int iswalnum(wint_t c) {
    return c <= 255 ? isalnum(c) : 0;
}

int iswalpha(wint_t c) {
    return c <= 255 ? isalpha(c) : 0;
}

int iswblank(wint_t c) {
    return c == L'\t' || c == L' ';
}

int iswcntrl(wint_t c) {
    return c <= 255 ? iscntrl(c) : 0;
}

int iswdigit(wint_t c) {
    return c <= 255 ? isdigit(c) : 0;
}

int iswgraph(wint_t c) {
    return c <= 255 ? isgraph(c) : 0;
}

int iswlower(wint_t c) {
    return c <= 255 ? islower(c) : 0;
}

int iswprint(wint_t c) {
    return c <= 255 ? isprint(c) : 0;
}

int iswpunct(wint_t c) {
    return c <= 255 ? ispunct(c) : 0;
}

int iswspace(wint_t c) {
    return c <= 255 ? isspace(c) : 0;
}

int iswupper(wint_t c) {
    return c <= 255 ? isupper(c) : 0;
}

int iswxdigit(wint_t c) {
    return c <= 255 ? isxdigit(c) : 0;
}

wint_t towlower(wint_t c) {
    return c <= 255 ? (wint_t) tolower(c) : c;
}

wint_t towupper(wint_t c) {
    return c <= 255 ? (wint_t) toupper(c) : c;
}
