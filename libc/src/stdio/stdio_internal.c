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
    int flags = 0;

    while (*mode) {
        switch(*mode++) {
            case 'r':
                flags |= O_RDONLY;
                break;
            case 'w':
                flags |= O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case 'a':
                flags |= O_WRONLY | O_APPEND | O_CREAT;
                break;
            case '+':
                flags |= O_RDWR;
                break;
            case 'x':
                flags |= O_EXCL;
                break;
            case 'e':
                flags |= O_CLOEXEC;
                break;
            case 'b':
                break;
            default:
                return -1;
        }
    }

    return flags;
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

static int fprintf_callback(union callback_data* cd, char c) {
	fputc(c, cd->stream);
	return 0;
}

static int sprintf_callback(union callback_data* cd, char c) {
    cd->buf.str[cd->buf.written] = c;
    cd->buf.written++;
    return 0;

}

static int snprintf_callback(union callback_data* cd, char c) {
    if (cd->buf.size > cd->buf.written + 1) {
        cd->buf.str[cd->buf.written] = c;

        cd->buf.written++;
        if (cd->buf.written < cd->buf.size) {
            cd->buf.str[cd->buf.written] = '\0';
        }
    }

    return 0;
}

static size_t print_dec(union callback_data* cd, int callback_type, unsigned long long value, unsigned int width, int fill_zero, int align_right, int precision) {
    int (*callback)(union callback_data*, char) = NULL;
    switch (callback_type) {
        case FPRINTF:
            callback = fprintf_callback;
            break;
        case SPRINTF:
            callback = sprintf_callback;
            break;
        case SNPRINTF:
            callback = snprintf_callback;
            break;
        default: __builtin_unreachable();
    }

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
            callback(cd, fill_zero ? '0' : ' ');
            written++;
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
            callback(cd, tmp[i]);
            written++;
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
            callback(cd, tmp[i]);
            written++;
            i++;
        }
        while (printed < (long long) width) {
            callback(cd, fill_zero ? '0' : ' ');
            written++;
            printed++;
        }
    }

    return written;
}

static size_t print_hex(union callback_data* cd, int callback_type, unsigned long long value, unsigned int width, int fill_zero, int alt, int caps, int align) {
    int (*callback)(union callback_data*, char) = NULL;
    switch (callback_type) {
        case FPRINTF:
            callback = fprintf_callback;
            break;
        case SPRINTF:
            callback = sprintf_callback;
            break;
        case SNPRINTF:
            callback = snprintf_callback;
            break;
        default: __builtin_unreachable();
    }

	size_t written = 0;
	int i = width;

	unsigned long long n_width = 1;
	unsigned long long j = 0x0f;
	while (value > j && j < UINT64_MAX) {
		n_width += 1;
		j *= 0x10;
		j += 0x0f;
	}

	if (!fill_zero && align == 1) {
		while (i > (long long) n_width + 2 * !!alt) {
            callback(cd, ' ');
            written++;
			i--;
		}
	}

	if (alt) {
        callback(cd, '0');
		callback(cd, caps ? 'X' : 'x');
        written += 2;
	}

	if (fill_zero && align == 1) {
		while (i > (long long) n_width + 2 * !!alt) {
            callback(cd, '0');
            written++;
			i--;
		}
	}

	i = (long long) n_width;
	while (i-- > 0) {
		char c = (caps ? "0123456789ABCDEF" : "0123456789abcdef")[(value >> (i * 4)) & 0xf];
        callback(cd, c);
        written++;
	}

	if (align == 0) {
		i = width;
		while (i > (long long) n_width + 2 * !!alt) {
            callback(cd, ' ');
            written++;
			i--;
		}
	}

	return written;
}

int __printf_internal(union callback_data* cd, int callback_type, const char* fmt, va_list args) {
    int (*callback)(union callback_data*, char) = NULL;
    switch (callback_type) {
        case FPRINTF:
            callback = fprintf_callback;
            break;
        case SPRINTF:
            callback = sprintf_callback;
            break;
        case SNPRINTF:
            callback = snprintf_callback;
            break;
        default: __builtin_unreachable();
    }

    char* s;
    size_t written = 0;

    unsigned long long ullval;
    long long llval;

    for (const char *f = fmt; *f != '\0'; f++) {
        if (*f != '%') {
            callback(cd, *f);
            written++;
            continue;
        }

        f++;

        unsigned int arg_width = 0;
        int align = 1;
        int fill_zero = 0;
        int big = 0;
        int alt = 0;
        int always_sign = 0;
        int precision = -1;

        for (;;) {
            if (*f == '-') {
                align = 0;
                f++;
            } else if (*f == '#') {
                alt = 1;
                f++;
            } else if (*f == '*') {
                arg_width = (int) va_arg(args, int);
                f++;
            } else if (*f == '0') {
                fill_zero = 1;
                f++;
            } else if (*f == '+') {
                always_sign = 1;
                f++;
            } else if (*f == ' ') {
                always_sign = 2;
                f++;
            } else {
                break;
            }
        }

        while (*f >= '0' && *f <= '9') {
            arg_width *= 10;
            arg_width += *f - '0';
            f++;
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
                              s = (char*) va_arg(args, char*);
                              if (s == NULL) {
                                  s = "(null)";
                              }

                              if (precision >= 0) {
                                  while (*s != '\0' && precision > 0) {
                                      callback(cd, *s++);
                                      written++;
                                      count++;
                                      precision--;
                                      if (arg_width && count == arg_width) {
                                          break;
                                      }
                                  }
                              } else {
                                  while (*s != '\0') {
                                      callback(cd, *s++);
                                      written++;
                                      count++;
                                      if (arg_width && count == arg_width) {
                                          break;
                                      }
                                  }
                              }
                          }
                          while (count < arg_width) {
                              callback(cd, ' ');
                              written++;
                              count++;
                          }
                      }
                      break;
            case 'c':
                callback(cd, (char) va_arg(args, int));
                break;
            case 'p':
                alt = 1;
                if (sizeof(void*) == sizeof(long long)) {
                    big = 2;
                }
                __attribute__((fallthrough));
            case 'X':
            case 'x':
                if (big == 2) {
                    ullval = (unsigned long long) va_arg(args, unsigned long long);
                } else if (big == 1) {
                    ullval = (unsigned long) va_arg(args, unsigned long);
                } else {
                    ullval = (unsigned int) va_arg(args, unsigned int);
                }
                written += print_hex(cd, callback_type, ullval, arg_width, fill_zero, alt, !(*f & 32), align);
                break;
            case 'i':
            case 'd':
                if (big == 2) {
                    llval = (long long) va_arg(args, long long);
                } else if (big == 1) {
                    llval = (long) va_arg(args, long);
                } else {
                    llval = (int) va_arg(args, int);
                }
                if (llval < 0) {
                    callback(cd, '-');
                    written++;
                    llval = -llval;
                } else if (always_sign) {
                    callback(cd, always_sign == 2 ? ' ' : '+');
                    written++;
                }
                written += print_dec(cd, callback_type, llval, arg_width, fill_zero, align, precision);
                break;
            case 'u':
                if (big == 2) {
                    ullval = (unsigned long long) va_arg(args, unsigned long long);
                } else if (big == 1) {
                    ullval = (unsigned long) va_arg(args, unsigned long);
                } else {
                    ullval = (unsigned int) va_arg(args, unsigned int);
                }
                written += print_dec(cd, callback_type, ullval, arg_width, fill_zero, align, precision);
                break;
            case '%':
                callback(cd, '%');
                written++;
                break;
            default:
                callback(cd, *f);
                written++;
                break;
        }
    }

    return written;
}
