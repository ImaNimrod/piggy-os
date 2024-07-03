#include <ctype.h> 

int isalnum(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isblank(int c) {
    return c == ' ' || c == '\t';
}

int iscntrl(int c) {
    return c <= 31 || c == 127;
}

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int isgraph(int c) {
    return c > ' ' && c != 127;
}

int islower(int c) {
    return c >= 'a' && c <= 'z';
}

int isprint(int c) {
    return c >= ' ' && c != 127 && c < 255;
}

int ispunct(int c) {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

int isspace(int c) {
    return c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == ' ';
}

int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

int isxdigit(int c) {
    return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9');
}

int tolower(int c) {
    return isupper(c) ? (c - 'A' + 'a') : c;
}

int toupper(int c) {
    return islower(c) ? (c - 'a' + 'A') : c;
}
