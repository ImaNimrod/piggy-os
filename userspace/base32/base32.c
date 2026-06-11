#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE (4 * 1024)

static const unsigned char BASE32_DECODE_TABLE[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 26,   27,   28,   29,   30,   31,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff,
    0xff, 0,    1,    2,    3,    4,    5,    6,
    7,    8,    9,    10,   11,   12,   13,   14,
    15,   16,   17,   18,   19,   20,   21,   22,
    23,   24,   25,   0xff, 0xff, 0xff, 0xff, 0xff,
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
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

__attribute__((nonstring)) static const unsigned char BASE32_ENCODE_TABLE[32] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static ssize_t decode_block(const unsigned char* in, ssize_t in_size, unsigned char* out) {
    if (in_size <= 0) {
        return 0;
    }

    uint64_t buffer = 0;
    int bits = 0;
    ssize_t out_len = 0;

    for (ssize_t i = 0; i < in_size; i++) {
        unsigned char c = in[i];
        if (c == '=') {
            break;
        }

        unsigned char val = BASE32_DECODE_TABLE[c];
        if (val == 0xff) {
            return -1;
        }

        buffer = (buffer << 5) | val;
        bits += 5;

        if (bits >= 8) {
            bits -= 8;
            out[out_len++] = (buffer >> bits) & 0xff;
        }
    }

    return out_len;
}

static int decode_file(const char* filename, int fd) {
    unsigned char in_buf[BUF_SIZE];
    unsigned char out_buf[BUF_SIZE];
    unsigned char block[8];

    ssize_t nread;
    ssize_t block_len = 0;

    while ((nread = read(fd, in_buf, sizeof(in_buf))) > 0) {
        for (ssize_t i = 0; i < nread; i++) {
            unsigned char c = in_buf[i];

            if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
                continue;
            }

            block[block_len++] = c;

            if (block_len == 8) {
                ssize_t out_len = decode_block(block, 8, out_buf);
                if (out_len < 0) {
                    warnx("invalid input");
                    return EXIT_FAILURE;
                }

                if (write(STDOUT_FILENO, out_buf, out_len) != out_len) {
                    err(EXIT_FAILURE, "write(stdout)");
                }

                block_len = 0;
            }
        }
    }

    if (nread < 0) {
        warn("read(%s)", filename);
        return EXIT_FAILURE;
    }

    if (block_len > 0) {
        ssize_t out_len = decode_block(block, block_len, out_buf);
        if (out_len < 0) {
            warnx("invalid input");
            return EXIT_FAILURE;
        }

        if (write(STDOUT_FILENO, out_buf, out_len) != out_len) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    return EXIT_SUCCESS;
}

static ssize_t encode_block(const unsigned char* in, ssize_t in_size, unsigned char* out) {
    if (in_size <= 0) {
        return 0;
    }

    uint64_t buffer = 0;
    int bits = 0;
    int out_len = 0;

    for (ssize_t i = 0; i < in_size; i++) {
        buffer = (buffer << 8) | in[i];
        bits += 8;
    }

    int total_bits = ((in_size * 8 + 4) / 5) * 5;
    buffer <<= (total_bits - bits);
    bits = total_bits;

    while (bits > 0) {
        bits -= 5;
        out[out_len++] = BASE32_ENCODE_TABLE[(buffer >> bits) & 0x1f];
    }

    while (out_len % 8 != 0) {
        out[out_len++] = '=';
    }

    return out_len;
}

static int encode_file(const char* filename, int fd) {
    unsigned char in_buf[BUF_SIZE];
    unsigned char out_buf[BUF_SIZE * 8 / 5 + 16];

    unsigned char carry[5];
    ssize_t leftover = 0;

    ssize_t nread;
    while ((nread = read(fd, in_buf, sizeof(in_buf))) > 0) {
        ssize_t i = 0;

        if (leftover > 0) {
            ssize_t needed = 5 - leftover;
            if (nread < needed) {
                for (ssize_t j = 0; j < nread; j++) {
                    carry[leftover + j] = in_buf[j];
                }

                leftover += nread;
                continue;
            }

            for (ssize_t j = 0; j < needed; j++) {
                carry[leftover + j] = in_buf[j];
            }

            ssize_t out_len = encode_block(carry, 5, out_buf);
            if (write(STDOUT_FILENO, out_buf, out_len) != out_len) {
                err(EXIT_FAILURE, "write(stdout)");
            }

            i += needed;
            leftover = 0;
        }

        for (; i + 5 <= nread; i += 5) {
            ssize_t out_len = encode_block(in_buf + i, 5, out_buf);
            if (write(STDOUT_FILENO, out_buf, out_len) != out_len) {
                err(EXIT_FAILURE, "write(stdout)");
            }
        }

        leftover = nread - i;
        for (ssize_t j = 0; j < leftover; j++) {
            carry[j] = in_buf[i + j];
        }
    }

    if (nread < 0) {
        warn("read(%s)", filename);
        return EXIT_FAILURE;
    }

    if (leftover > 0) {
        ssize_t out_len = encode_block(carry, leftover, out_buf);
        if (write(STDOUT_FILENO, out_buf, out_len) != out_len) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: base32 [-d] [FILE]\n");
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

    int ret = decode ? decode_file(filename, fd) : encode_file(filename, fd);

    if (fd != STDIN_FILENO) {
        close(fd);
    }

    return ret;
}
