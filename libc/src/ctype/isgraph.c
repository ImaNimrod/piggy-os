#include <ctype.h>

int isgraph(int c) {
    return c > ' ' && c != 127;
}
