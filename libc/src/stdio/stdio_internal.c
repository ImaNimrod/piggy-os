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
