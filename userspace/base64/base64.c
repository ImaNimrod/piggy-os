#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE (4 * 1024)

static const char* BASE64_ENCODE_TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const uint8_t BASE64_DECODE_TABLE[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 62,   0xff, 0xff, 0xff, 63,
    52,   53,   54,   55,   56,   57,   58,   59,
    60,   61,   0xff, 0xff, 0xff, 0xfe, 0xff, 0xff,
    0xff, 0,    1,    2,    3,    4,    5,    6,
    7,    8,    9,    10,   11,   12,   13,   14,
    15,   16,   17,   18,   19,   20,   21,   22,
    23,   24,   25,   0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 26,   27,   28,   29,   30,   31,   32,
    33,   34,   35,   36,   37,   38,   39,   40,
    41,   42,   43,   44,   45,   46,   47,   48,
    49,   50,   51,   0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static inline bool is_whitespace(unsigned char c) {
    return c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

static ssize_t decode_block(const unsigned char* in, ssize_t in_size, unsigned char* out) {
    ssize_t in_index = 0;
    ssize_t out_index = 0;

    while (in_index + 4 <= in_size) {
        uint8_t a = BASE64_DECODE_TABLE[in[in_index + 0]];
        uint8_t b = BASE64_DECODE_TABLE[in[in_index + 1]];
        uint8_t c = BASE64_DECODE_TABLE[in[in_index + 2]];
        uint8_t d = BASE64_DECODE_TABLE[in[in_index + 3]];

        if ((a >= 0xff) || (b >= 0xff) || ((c >= 0xff) && c != 0xfe) || ((d >= 0xff) && d != 0xfe)) {
            return -1;
        }

        out[out_index++] = (a << 2) | ((b >> 4) & 0x03);

        if (in[in_index + 2] != '=') {
            out[out_index++] = ((b & 0x0f) << 4) | ((c >> 2) & 0x0f);
        }

        if (in[in_index + 3] != '=') {
            out[out_index++] = ((c & 0x03) << 6) | d;
        }

        in_index += 4;
    }

    return out_index;
}

static ssize_t decode_final(const unsigned char* in, ssize_t in_size, unsigned char* out) {
    if (in_size < 2) {
        return -1;
    }

    uint8_t a = BASE64_DECODE_TABLE[in[0]];
    uint8_t b = BASE64_DECODE_TABLE[in[1]];
    uint8_t c = (in_size > 2) ? BASE64_DECODE_TABLE[in[2]] : 0xfe;
    uint8_t d = (in_size > 3) ? BASE64_DECODE_TABLE[in[3]] : 0xfe;

    if ((a >= 0xff) || (b >= 0xff) || (c >= 0xff && c != 0xfe) || (d >= 0xff && d != 0xfe)) {
        return -1;
    }

    uint32_t v = (a << 18) | (b << 12);

    ssize_t out_index = 0;
    out[out_index++] = (v >> 16) & 0xff;

    if (in_size > 2 && in[2] != '=') {
        v |= c << 6;
        out[out_index++] = (v >> 8) & 0xff;
    }

    if (in_size > 3 && in[3] != '=') {
        v |= d;
        out[out_index++] = v & 0xff;
    }

    return out_index;
}

static int decode_file(int fd, char* filename) {
    unsigned char in_buf[BUF_SIZE + 4];
    unsigned char out_buf[(BUF_SIZE * 3) / 4];

    ssize_t carry = 0;

    ssize_t nread;
    while ((nread = read(fd, in_buf + carry, BUF_SIZE - carry)) > 0) {
        ssize_t j = 0;
        for (ssize_t i = 0; i < carry + nread; i++) {
            if (!is_whitespace(in_buf[i])) {
                in_buf[j++] = in_buf[i];
            }
        }

        ssize_t full = (j / 4) * 4;

        ssize_t out_size = decode_block(in_buf, full, out_buf);
        if (out_size < 0) {
            warnx("invalid input");
            return EXIT_FAILURE;
        }

        if (write(STDOUT_FILENO, out_buf, out_size) != out_size) {
            err(EXIT_FAILURE, "write(stdout)");
        }

        carry = j - full;
        if (carry > 0) {
            memmove(in_buf, in_buf + full, carry);
        }
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    if (carry > 0) {
        ssize_t out_size = decode_final(in_buf, carry, out_buf);
        if (out_size < 0) {
            warnx("invalid input");
            return EXIT_FAILURE;
        }

        if (write(STDOUT_FILENO, out_buf, out_size) != out_size) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    return EXIT_SUCCESS;
}

static ssize_t encode_block(const unsigned char* in, ssize_t in_size, unsigned char* out) {
    ssize_t i;
    ssize_t o = 0;

    for (i = 0; i + 2 < in_size; i += 3) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8) | (uint32_t) in[i + 2];
        out[o++] = BASE64_ENCODE_TABLE[(v >> 18) & 0x3f];
        out[o++] = BASE64_ENCODE_TABLE[(v >> 12) & 0x3f];
        out[o++] = BASE64_ENCODE_TABLE[(v >> 6)  & 0x3f];
        out[o++] = BASE64_ENCODE_TABLE[v & 0x3f];
    }

    if (i < in_size) {
        uint32_t v = in[i] << 16;
        out[o++] = BASE64_ENCODE_TABLE[(v >> 18) & 0x3f];

        if (i + 1 < in_size) {
            v |= in[i+1] << 8;
            out[o++] = BASE64_ENCODE_TABLE[(v >> 12) & 0x3f];
            out[o++] = BASE64_ENCODE_TABLE[(v >> 6) & 0x3f];
            out[o++] = '=';
        } else {
            out[o++] = BASE64_ENCODE_TABLE[(v >> 12) & 0x3f];
            out[o++] = '=';
            out[o++] = '=';
        }
    }

    return o;
}

static int encode_file(int fd, char* filename) {
    unsigned char in_buf[BUF_SIZE];
    unsigned char out_buf[((BUF_SIZE * 4) / 3) + 4];

    ssize_t nread;
    while ((nread = read(fd, in_buf, sizeof(in_buf))) > 0) {
        ssize_t out_size = encode_block(in_buf, nread, out_buf);
        if (write(STDOUT_FILENO, out_buf, out_size) != out_size) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: base64 [-d] [FILE]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool decode = false;

    int c;
    while ((c = getopt(argc, argv, "d")) != -1) {
        switch (c) {
            case 'd':
                decode = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    char* filename;
    int fd;

    if (argc < 1 || strcmp(argv[0], "-") == 0) {
        filename = "stdin";
        fd = STDIN_FILENO;
    } else {
        filename = argv[0];
        fd = open(filename, O_RDONLY);
        if (fd < 0) {
            err(EXIT_FAILURE, "%s", filename);
        }
    }

    int ret = EXIT_SUCCESS;

    if (decode) {
        ret = decode_file(fd, filename);
    } else {
        ret = encode_file(fd, filename);
    }

    if (fd != STDIN_FILENO) {
        close(fd);
    }

    return ret;
}
