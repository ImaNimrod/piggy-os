#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h> 
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 4096

static void usage(void) {
    fprintf(stderr, "usage: shred [-n N] [-r RNG_SRC] [-uvz] FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    uintmax_t iterations = 3;
    char* random_path = "/dev/random";
    bool remove_file = false;
    bool verbose = false;
    bool zero = false;

    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "n:r:uvz")) != -1) {
        switch (c) {
            case 'n':
                errno = 0;
                iterations = strtoumax(optarg, &end_ptr, 10);
                if (errno != 0 || end_ptr == optarg || *end_ptr) {
                    warnx("invalid iteration count: '%s'", optarg);
                    usage();
                }
                break;
            case 'r':
                random_path = optarg;
                break;
            case 'u':
                remove_file = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'z':
                zero = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    if (zero) {
        iterations++;
    }

    int rng_fd = open(random_path, O_RDONLY);
    if (rng_fd < 0) {
        err(EXIT_FAILURE, "%s", random_path);
    }

    int ret = EXIT_SUCCESS;

    for (int i = 0; i < argc; i++) {
        char* filename = argv[i];

        int fd = open(filename, O_WRONLY);
        if (fd < 0) {
            warn("%s", filename);
            ret = EXIT_FAILURE;
            continue;
        }

        struct stat st;
        if (fstat(fd, &st) < 0) {
            warn("%s", filename);
            ret = EXIT_FAILURE;
            close(fd);
            continue;
        }

        off_t file_size = st.st_size;
        if (file_size == 0) {
            close(fd);
            continue;
        }

        char buf[BUF_SIZE];

        for (uintmax_t j = 0; j < iterations; j++) {
            bool is_zero_pass = zero && j == iterations - 1;

            if (verbose) {
                warnx("%s: pass %ju/%ju (%s)", filename, j + 1, iterations, is_zero_pass ? "zeros" : "random");
            }

            if (is_zero_pass) {
                memset(buf, 0, sizeof(buf));

                off_t total_written = 0;
                while (total_written < file_size) {
                    size_t bytes_left = file_size - total_written;
                    size_t chunk_size = (bytes_left < sizeof(buf)) ? bytes_left : sizeof(buf);

                    ssize_t to_write = chunk_size;
                    char* p = buf;
                    while (to_write > 0) {
                        ssize_t nwritten = write(fd, p, to_write);
                        if (nwritten <= 0) {
                            warn("%s", filename);
                            break;
                        }

                        to_write -= nwritten;
                        p += nwritten;
                        total_written += nwritten;
                    }
                }
            } else {
                off_t total_written = 0;
                while (total_written < file_size) {
                    size_t bytes_left = file_size - total_written;
                    size_t chunk_size = (bytes_left < sizeof(buf)) ? bytes_left : sizeof(buf);

                    ssize_t nread = read(rng_fd, buf, chunk_size);
                    if (nread < 0) {
                        warn("read(%s)", random_path);
                        ret = EXIT_FAILURE;
                        break;
                    } else if (nread == 0) {
                        ret = EXIT_FAILURE;
                        break;
                    }

                    ssize_t to_write = nread;
                    char* p = buf;
                    while (to_write > 0) {
                        ssize_t nwritten = write(fd, p, to_write);
                        if (nwritten <= 0) {
                            warn("%s", filename);
                            ret = EXIT_FAILURE;
                            break;
                        }

                        to_write -= nwritten;
                        p += nwritten;
                        total_written += nwritten;
                    }
                }
            }

            if (lseek(fd, 0, SEEK_SET) < 0) {
                warn("%s", filename);
                ret = EXIT_FAILURE;
                break;
            }

            if (fsync(fd) < 0) {
                warn("%s", filename);
                ret = EXIT_FAILURE;
                break;
            }
        }

        if (remove_file) {
            if (ftruncate(fd, 0) < 0) {
                warn("%s", filename);
                ret = EXIT_FAILURE;
                close(fd);
                continue;
            }

            if (unlink(filename) < 0) {
                warn("%s", filename);
                ret = EXIT_FAILURE;
                close(fd);
                continue;
            }

            if (verbose) {
                warnx("%s: removed", filename);
            }
        }

        close(fd);
    }

    return ret;
}
