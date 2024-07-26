#include <ctype.h>
#include <stdlib.h>

long atol(const char* str) {
	int n = 0;
	int neg = 0;

	while (isspace(*str)) {
		str++;
	}
 
    if (*str == '-') {
        neg = 1;
    } else if (*str == '+') {
        str++;
    }

	while (isdigit(*str)) {
		n = 10 * n - (*str++ - '0');
	}

	return neg ? n : -n;
}
