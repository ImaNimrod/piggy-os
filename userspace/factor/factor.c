#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void print_factors(uintmax_t n) {
    printf("%ju: ", n);

    while (n % 2 == 0) {
        printf("2 ");
        n /= 2;
    }

    for (uintmax_t i = 3; i * i <= n; i += 2) {
        while (n % i == 0) {
            printf("%ju ", i);
            n /= i;
        }
    }

    if (n > 1) {
        printf("%ju", n);
    }

    putchar('\n');
}

static void usage(void) {
    fprintf(stderr, "usage: factor [NUMBER]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    int ret = EXIT_SUCCESS;

    char* end_ptr;
    uintmax_t number = 0;

    if (argc > 0) {
        for (int i = 0; i < argc; i++) {
            errno = 0;

            number = strtoumax(argv[i], &end_ptr, 10);
            if (errno != 0 || argv[i] == end_ptr) {
                warnx("'%s' is not a valid positive integer", argv[i]);
                ret = EXIT_FAILURE;
            }

            print_factors(number);
        }
    } else {
        for (;;) {
            int res = scanf("%ju", &number);
            if (res == 1) {
                print_factors(number);
            } else if (res == EOF) {
                break;
            } else {
                warnx("input is not a valid positive integer");
                ret = EXIT_FAILURE;

                int c;
                while ((c = getchar()) != '\n' && c != EOF) {}
            }
        }
    }

    return ret;
}
