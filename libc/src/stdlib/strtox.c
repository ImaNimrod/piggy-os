#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include "stdlib_internal.h"

static int is_valid(int base, char c) {
	if (c < '0') {
        return false;
    }
	if (base <= 10) {
		return c < ('0' + base);
	}

	if (c >= 'a' && c < 'a' + (base - 10)) {
        return true;
    }
	if (c >= 'A' && c < 'A' + (base - 10)) {
        return true;
    }
	if (c >= '0' && c <= '9') {
        return true;
    }
	return false;
}

static int convert_digit(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'Z') {
		return c - 'A' + 10;
	}
	return 0;
}

#define strtox(max, type) \
	if (base < 0 || base == 1 || base > 36) { \
		errno = EINVAL; \
		return max; \
	} \
	while (*nptr && isspace(*nptr)) nptr++; \
	int sign = 1; \
	if (*nptr == '-') { \
		sign = -1; \
		nptr++; \
	} else if (*nptr == '+') { \
		nptr++; \
	} \
	if (base == 16) { \
		if (*nptr == '0') { \
			nptr++; \
			if (*nptr == 'x') { \
				nptr++; \
			} \
		} \
	} \
	if (base == 0) { \
		if (*nptr == '0') { \
			base = 8; \
			nptr++; \
			if (*nptr == 'x') { \
				base = 16; \
				nptr++; \
			} \
		} else { \
			base = 10; \
		} \
	} \
	type val = 0; \
	while (is_valid(base, *nptr)) { \
		val *= base; \
		val += convert_digit(*nptr); \
		nptr++; \
	} \
	if (endptr) { \
		*endptr = (char *)nptr; \
	} \
	if (sign == -1) { \
		return -val; \
	} else { \
		return val; \
	}

long int strtol(const char* nptr, char** endptr, int base) {
    strtox(LONG_MAX, unsigned long int);
}

long long int strtoll(const char* nptr, char** endptr, int base) {
    strtox(LLONG_MAX, unsigned long long int);
}

unsigned long int strtoul(const char* nptr, char** endptr, int base) {
    strtox(ULONG_MAX, unsigned long int);
}

unsigned long long int strtoull(const char* nptr, char** endptr, int base) {
    strtox(ULLONG_MAX, unsigned long int);
}
