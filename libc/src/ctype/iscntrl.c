#include <ctype.h>

int iscntrl(int c) {
    return c <= 31 || c == 127;
}
