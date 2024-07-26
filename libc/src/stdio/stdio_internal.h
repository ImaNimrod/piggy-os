#ifndef STDIO_INTERNAL_H
#define STDIO_INTERNAL_H

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define FILE_FLAG_ERROR     (1 << 0)
#define FILE_FLAG_EOF       (1 << 1)
#define FILE_FLAG_BUFFERED  (1 << 2)
#define FILE_FLAG_TTY       (1 << 3)

#define UNGET_BYTES 8

struct __FILE {
	int fd;
    int flags;
    unsigned char* buf;
    size_t buf_size;
    size_t read_pos;
    size_t read_end;
    size_t write_pos;
	struct __FILE* prev;
	struct __FILE* next;
};

extern FILE* __file_list_head;

int __fopen_mode_to_flags(const char*);
size_t __read_bytes(FILE*, unsigned char*, size_t);
size_t __write_bytes(FILE*, const unsigned char*, size_t);
int __vafprintf(FILE*, const char*, va_list);
int __vasnprintf(char*, size_t, const char*, va_list);

#endif /* STDIO_INTERNAL_H */
