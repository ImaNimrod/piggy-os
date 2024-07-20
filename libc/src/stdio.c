#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FILE_FLAG_ERROR (1 << 0)
#define FILE_FLAG_EOF   (1 << 1)
#define FILE_FLAG_NFREE (1 << 2)

struct _FILE {
    int fd;
    int flags;
    int bufmode;
    int bufsize;
    int nr;
    int nw;
    char* ptr;
    char* buf;
};

FILE stdio_streams[3] = {
    { 0, 0, _IOLBF, BUFSIZ, 0, 0, NULL, NULL },
    { 1, 0, _IOLBF, BUFSIZ, 0, 0, NULL, NULL },
    { 2, 0, _IONBF, 1, 0, 0, NULL, NULL }
};

FILE* stdin = &stdio_streams[0];
FILE* stdout = &stdio_streams[1];
FILE* stderr = &stdio_streams[2];

static int _fillbuf(FILE* f) {
    if (f->buf == NULL) {
        f->buf = malloc(f->bufsize);
        if (f->buf == NULL) {
            return EOF;
        }
    }

    f->ptr = f->buf;
    f->nr = read(f->fd, f->ptr, f->bufsize);

    if (--f->nr < 0) {
        if (f->nr == -1) {
            f->flags |= FILE_FLAG_EOF;
        } else {
            f->flags |= FILE_FLAG_ERROR;
        }

        f->nr = 0;
        f->nw = 0;
        return EOF;
    }

    return (unsigned char) *f->ptr++;
}

int fclose(FILE* f) {
    if (f->buf != NULL) {
        if (f->nw != 0) {
            fflush(f);
        }

        if ((f->flags & FILE_FLAG_NFREE) == 0) {
            free(f->buf);
        }

        f->buf = NULL;
    }

    return close(f->fd);
}

int feof(FILE* f) {
    return (f->flags & FILE_FLAG_EOF);
}

int ferror(FILE* f) {
    return (f->flags & FILE_FLAG_ERROR);
}

int fflush(FILE* f) {
    int ret = 0;
    ssize_t n;
    char* p = f->buf;

    while (f->nw > 0) {
        if ((n = write(f->fd, p, f->nw)) < 0) {
            f->flags |= FILE_FLAG_ERROR;
            ret = EOF;
            break;
        }

        if (n > f->nw) {
            n = f->nw;
        }
        f->nw -= n;

        p += n;
    }

    f->ptr = f->buf;
    f->nw = 0;
    f->nr = 0;
    return ret;
}

int fgetc(FILE* f) {
    if (f->nw > 0) {
        fflush(f);
    }
    return (--(f)->nr >= 0) ? (unsigned char) *f->ptr++ : _fillbuf(f);
}

char* fgets(char *str, int size, FILE *fp) {
    int i;
    for (i = 0; i < size - 1; i++) {
        str[i] = fgetc(fp);
        if (str[i] == EOF || str[i] == '\n') {
            break;
        }
    }

    if (i == 0) {
        return NULL;
    }

    str[i] = '\0';
    return str;
}

int fileno(FILE* f) {
    return f->fd;
}

FILE* fopen(const char* path, const char* mode) {
    int flags;
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        flags = O_RDONLY;
    } else if (strcmp(mode, "r+") == 0 || strcmp(mode, "r+b") == 0 ||
               strcmp(mode, "rb+") == 0) {
        flags = O_RDWR;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strcmp(mode, "w+") == 0 || strcmp(mode, "w+b") == 0 ||
               strcmp(mode, "wb+") == 0) {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else if (strcmp(mode, "a+") == 0 || strcmp(mode, "a+b") == 0 ||
               strcmp(mode, "ab+") == 0) {
        flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        return NULL;
    }

    FILE* f = malloc(sizeof(*f));
    if (f == NULL) {
        return NULL;
    }

    int fd = open(path, flags);
    if (fd < 0) {
        free(f);
        return NULL;
    }

    f->fd = fd;
    f->flags = 0;
    f->bufmode = _IOFBF;
    f->bufsize = BUFSIZ;
    f->nr = 0;
    f->nw = 0;
    f->buf = NULL;
    f->ptr = NULL;
    return f;
}

int fputc(int c, FILE* f) {
    if (f->buf == NULL) {
        f->buf = (char*) malloc(f->bufsize);
        if (f->buf == NULL) {
            return EOF;
        }

        f->nr = 0;
        f->nw = 0;
        f->ptr = f->buf;
    }

    if (f->nr > 0) {
        lseek(f->fd, SEEK_CUR, -f->nr);
        f->nr = 0;
        f->nw = 0;
        f->ptr = f->buf; 
    }
    
    *f->ptr++ = c;

    if (f->nw++ >= f->bufsize ||
        (f->bufmode == _IOLBF && (char) c == '\n') ||
        f->bufmode == _IONBF) {

        if (fflush(f) != 0) {
            c = EOF;
        }
    }

    return (unsigned char) c;
}

int fputs(const char* str, FILE* f) {
    int status = 0;

    for (int i = 0; i < (int) strlen(str); i++) {
        status = fputc(str[i], f);
        if (status == EOF) {
            break;
        }
    }

    return status;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    char* buf = (char*) ptr;
    int stop = 0;

    size_t n = 0;
    while (n < nmemb && stop == 0) {
        size_t s = 0;
        int c;
        while (s < size) {
            if ((c = fgetc(f)) == EOF) {
                return n;
            }

            buf[s++] = (unsigned char) c;
            if (f->bufmode == _IOLBF && c == '\n') {
                stop = 1;
                break;
            }
        }

        buf += size;
        n++;
    }

    return n;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    const char* buf = (const char*) ptr;

    size_t n = 0;
    while (n < nmemb) {
        size_t s = 0;
        while (s < size) {
            if (fputc(buf[s], f) == EOF) {
                return n;
            }
            s++;
        }

        buf += size;
        n++;
    }

    return n;
}

int getchar(void) {
    return fgetc(stdin);
}

char* gets(char* str) {
    return fgets(str, (int) -1, stdin);
}

int putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char* str) {
    return fputs(str, stdout);
}
