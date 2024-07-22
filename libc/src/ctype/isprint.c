#include <ctype.h>

int isprint(int c) {
    return c >= ' ' && c != 127 && c < 255;
}
