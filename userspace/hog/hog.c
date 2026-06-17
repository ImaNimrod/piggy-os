#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//#include <json-c/json_util.h>

/*
static int install_package(const char* package) {
    (void) package;
    printf("installing packages from remote repository not implemented\n");
    return EXIT_SUCCESS;
}

static int install_recipe(const char* recipe) {
    int fd = open(recipe, O_RDONLY);
    if (fd < 0) {
        warn("%s", recipe);
        return EXIT_FAILURE;
    }

    struct json_object* object = json_object_from_fd(fd);
    if (object == NULL) {
        warn("%s", json_util_get_last_err());
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}

static int install_command(int argc, char* argv[]) {
    if (argc < 1) {
        warnx("missing package name");
        return EXIT_FAILURE;
    }

    if (strcmp(argv[0], "-l") == 0) {
        if (argc < 2) {
            warnx("missing recipe file path");
        }

        return install_recipe(argv[1]);
    } else {
        return install_package(argv[0]);
    }
}

static void usage(void) {
    fprintf(stderr, "usage: hog COMMAND [SUBCOMMAND]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("extra operands provided");
        usage();
    }

    int ret = EXIT_SUCCESS;

    const char* command = argv[0];

    if (strcmp(command, "install") == 0) {
        ret = install_command(argc - 1, &argv[1]);
    } else {
        warnx("unknown command: %s\n", command);
        usage();
    }

    return ret;
}
*/

int main() {}
