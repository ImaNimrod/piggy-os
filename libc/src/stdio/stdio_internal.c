#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "stdio_internal.h"

static FILE __stdin = {
    .fd = STDIN_FILENO,
    .flags = FILE_FLAG_BUFFERED,
    .buf_size = BUFSIZ,
    .read_pos = UNGET_BYTES,
    .read_end = UNGET_BYTES,
};

static FILE __stdout = {
    .fd = STDOUT_FILENO,
    .flags = FILE_FLAG_BUFFERED | FILE_FLAG_TTY,
    .buf_size = BUFSIZ,
    .read_pos = UNGET_BYTES,
    .read_end = UNGET_BYTES,
};

static FILE __stderr = {
    .fd = STDOUT_FILENO,
    .buf_size = UNGET_BYTES,
    .read_pos = UNGET_BYTES,
    .read_end = UNGET_BYTES,
};

FILE* stdin = &__stdin;
FILE* stdout = &__stdout;
FILE* stderr = &__stderr;

FILE* __file_list_head;

int __fopen_mode_to_flags(const char* mode) {
    int res = 0;

    while (*mode) {
        switch(*mode++) {
            case 'r':
                res |= O_RDONLY;
                break;
            case 'w':
                res |= O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case 'a':
                res |= O_WRONLY | O_APPEND | O_CREAT;
                break;
            case '+':
                res |= O_RDWR;
                break;
            case 'x':
                res|= O_EXCL;
                break;
            case 'e':
                res|= O_CLOEXEC;
                break;
            case 'b':
                break;
            default:
                return -1;
        }
    }

    return res;
}

void __init_stdio_buffers(void) {
    stdin->buf = malloc(stdin->buf_size);
    stdout->buf = malloc(stdout->buf_size);
    stderr->buf = malloc(stderr->buf_size);
}

void __cleanup_stdio(void) {
    if (stdout) {
        fflush(stdout);
    }
    if (stderr) {
        fflush(stderr);
    }

    while (__file_list_head) {
        fclose(__file_list_head);
    }
}

size_t __read_bytes(FILE* stream, unsigned char* buf, size_t size) {
    ssize_t res = read(stream->fd, buf, size);

    if (res == 0) {
        stream->flags |= FILE_FLAG_EOF;
        return 0;
    } else if (res < 0) {
        stream->flags |= FILE_FLAG_ERROR;
        return 0;
    } 

    return res;
}

size_t __write_bytes(FILE* stream, const unsigned char* buf, size_t size) {
    size_t written = 0;

    while (written < size) {
        ssize_t res = write(stream->fd, buf, size - written);
        if (res < 0) {
            stream->flags |= FILE_FLAG_ERROR;
            return written;
        }

        written += res;
        buf += res;
    }

    return written;
}

// TODO: actually implement printf function internals
static size_t print_dec(FILE* stream, unsigned long long value, unsigned int width, int fill_zero, int align_right, int precision) {
	size_t written = 0;

	unsigned long long n_width = 1;
	unsigned long long i = 9;

	if (precision == -1) {
        precision = 1;
    }

	if (value == 0) {
		n_width = 0;
	} else {
		unsigned long long val = value;
		while (val >= 10UL) {
			val /= 10UL;
			n_width++;
		}
	}

	if (n_width < (unsigned long long) precision) {
        n_width = precision;
    }

	int printed = 0;
	if (align_right) {
		while (n_width + printed < width) {
            fputc(fill_zero ? '0' : ' ', stream);
			printed += 1;
		}

		i = n_width;
		char tmp[100];
		while (i > 0) {
			unsigned long long n = value / 10;
			long long r = value % 10;
			tmp[i - 1] = r + '0';
			i--;
			value = n;
		}
		while (i < n_width) {
            fputc(tmp[i], stream);
			i++;
		}
	} else {
		i = n_width;
		char tmp[100];
		while (i > 0) {
			unsigned long long n = value / 10;
			long long r = value % 10;
			tmp[i - 1] = r + '0';
			i--;
			value = n;
			printed++;
		}
		while (i < n_width) {
            fputc(tmp[i], stream);
			i++;
		}
		while (printed < (long long)width) {
            fputc(fill_zero ? '0' : ' ', stream);
			printed += 1;
		}
	}

	return written;
}

static size_t print_hex(FILE* stream, unsigned long long value, unsigned int width, int fill_zero, int alt, int caps, int align) {
	size_t written = 0;
	int i = width;

	unsigned long long n_width = 1;
	unsigned long long j = 0x0F;
	while (value > j && j < UINT64_MAX) {
		n_width += 1;
		j *= 0x10;
		j += 0x0F;
	}

	if (!fill_zero && align == 1) {
		while (i > (long long) n_width + 2 * !!alt) {
            fputc(' ', stream);
			i--;
		}
	}

	if (alt) {
        fputc('0', stream);
		fputc(caps ? 'X' : 'x', stream);
	}

	if (fill_zero && align == 1) {
		while (i > (long long) n_width + 2 * !!alt) {
            fputc('0', stream);
			i--;
		}
	}

	i = (long long) n_width;
	while (i-- > 0) {
		char c = (caps ? "0123456789ABCDEF" : "0123456789abcdef")[(value >> (i * 4)) & 0xf];
        fputc(c, stream);
	}

	if (align == 0) {
		i = width;
		while (i > (long long) n_width + 2 * !!alt) {
            fputc(' ', stream);
			i--;
		}
	}

	return written;
}

int __vafprintf(FILE* stream, const char* fmt, va_list args) {
    char * s;
    size_t written = 0;
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') {
            fputc(*f, stream);
            continue;
        }

        f++;

        unsigned int arg_width = 0;
        int align = 1; /* right */
        int fill_zero = 0;
        int big = 0;
        int alt = 0;
        int always_sign = 0;
        int precision = -1;
        while (1) {
            if (*f == '-') {
                align = 0;
                ++f;
            } else if (*f == '#') {
                alt = 1;
                ++f;
            } else if (*f == '*') {
                arg_width = (int) va_arg(args, int);
                ++f;
            } else if (*f == '0') {
                fill_zero = 1;
                ++f;
            } else if (*f == '+') {
                always_sign = 1;
                ++f;
            } else if (*f == ' ') {
                always_sign = 2;
                ++f;
            } else {
                break;
            }
        }
        while (*f >= '0' && *f <= '9') {
            arg_width *= 10;
            arg_width += *f - '0';
            ++f;
        }
        if (*f == '.') {
            f++;
            precision = 0;
            if (*f == '*') {
                precision = (int) va_arg(args, int);
                f++;
            } else  {
                while (*f >= '0' && *f <= '9') {
                    precision *= 10;
                    precision += *f - '0';
                    f++;
                }
            }
        }

        if (*f == 'l') {
            big = 1;
            f++;
            if (*f == 'l') {
                big = 2;
                f++;
            }
        }
        if (*f == 'z') {
            big = (sizeof(size_t) == sizeof(unsigned long long) ? 2 :
                    sizeof(size_t) == sizeof(unsigned long) ? 1 : 0);
            f++;
        }
        if (*f == 't') {
            big = (sizeof(ptrdiff_t) == sizeof(unsigned long long) ? 2 :
                    sizeof(ptrdiff_t) == sizeof(unsigned long) ? 1 : 0);
            f++;
        }

        switch (*f) {
            case 's': {
                          size_t count = 0;
                          if (big) {
                              return written;
                          } else {
                              s = (char*) va_arg(args, char *);
                              if (s == NULL) {
                                  s = "(null)";
                              }

                              if (precision >= 0) {
                                  while (*s && precision > 0) {
                                      fputc(*s++, stream);
                                      count++;
                                      precision--;
                                      if (arg_width && count == arg_width) break;
                                  }
                              } else {
                                  while (*s) {
                                      fputc(*s++, stream);
                                      count++;
                                      if (arg_width && count == arg_width) break;
                                  }
                              }
                          }
                          while (count < arg_width) {
                              fputc(' ', stream);
                              count++;
                          }
                      }
                      break;
            case 'c': /* Single character */
                fputc((char) va_arg(args, int), stream);
                break;
            case 'p':
                alt = 1;
                if (sizeof(void*) == sizeof(long long)) big = 2; /* fallthrough */
            case 'X':
            case 'x': /* Hexadecimal number */
                {
                    unsigned long long val;
                    if (big == 2) {
                        val = (unsigned long long)va_arg(args, unsigned long long);
                    } else if (big == 1) {
                        val = (unsigned long)va_arg(args, unsigned long);
                    } else {
                        val = (unsigned int)va_arg(args, unsigned int);
                    }
                    written += print_hex(stream, val, arg_width, fill_zero, alt, !(*f & 32), align);
                }
                break;
            case 'i':
            case 'd': /* Decimal number */
                {
                    long long val;
                    if (big == 2) {
                        val = (long long)va_arg(args, long long);
                    } else if (big == 1) {
                        val = (long)va_arg(args, long);
                    } else {
                        val = (int)va_arg(args, int);
                    }
                    if (val < 0) {
                        fputc('-', stream);
                        val = -val;
                    } else if (always_sign) {
                        fputc(always_sign == 2 ? ' ' : '+', stream);
                    }
                    written += print_dec(stream, val, arg_width, fill_zero, align, precision);
                }
                break;
            case 'u': /* Unsigned ecimal number */
                {
                    unsigned long long val;
                    if (big == 2) {
                        val = (unsigned long long)va_arg(args, unsigned long long);
                    } else if (big == 1) {
                        val = (unsigned long)va_arg(args, unsigned long);
                    } else {
                        val = (unsigned int)va_arg(args, unsigned int);
                    }
                    written += print_dec(stream, val, arg_width, fill_zero, align, precision);
                }
                break;
            case '%': /* Escape */
                fputc('%', stream);
                break;
            default: /* Nothing at all, just dump it */
                fputc(*f, stream);
                break;
        }
    }
    return written;
}

int __vasnprintf(char* str, size_t size, const char* fmt, va_list args) {
    return -1;
}
