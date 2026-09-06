#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <termios.h> 
#include <unistd.h>

#define CTRL(c) ((c) & 0x1f)

static void usage(void) {
    fprintf(stderr, "usage: getty [TTY]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    const char* tty_filename = _PATH_TTY;
    if (argc >= 1) {
        tty_filename = argv[0];
    }

    int tty_fd = open(tty_filename, O_RDWR);
    if (tty_fd < 0) {
        err(EXIT_FAILURE, "open");
    }

    if (dup2(tty_fd, STDIN_FILENO) < 0) {
        err(EXIT_FAILURE, "dup2");
    }
    if (dup2(tty_fd, STDOUT_FILENO) < 0) {
        err(EXIT_FAILURE, "dup2");
    }
    if (dup2(tty_fd, STDERR_FILENO) < 0) {
        err(EXIT_FAILURE, "dup2");
    }

    if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0) {
        err(EXIT_FAILURE, "tcsetpgrp(%s)", tty_filename);
    }

    struct termios sane_termios = {
        .c_iflag = BRKINT | ICRNL | IXANY | IXON,
        .c_oflag = OPOST | ONLCR,
        .c_cflag = B38400 | CREAD | CS8,
        .c_lflag = ECHO | ECHOE | ECHOK | ECHONL | ICANON | IEXTEN | ISIG,
        .c_cc[VEOF] = CTRL('D'),
        .c_cc[VEOL] = '\0',
        .c_cc[VERASE] = 127,
        .c_cc[VINTR] = CTRL('C'),
        .c_cc[VKILL] = CTRL('U'),
        .c_cc[VMIN] = 1,
        .c_cc[VQUIT] = CTRL('\\'),
        .c_cc[VSTART] = CTRL('Q'),
        .c_cc[VSTOP] = CTRL('S'),
        .c_cc[VSUSP] = CTRL('Z'),
        .c_cc[VTIME] = 0,
    };

    if (tcsetattr(tty_fd, TCSANOW, &sane_termios) < 0) {
        err(EXIT_FAILURE, "tcsetattr(%s)", tty_filename);
    }

    close(tty_fd);

    if (chdir(getenv("HOME"))) {
        err(EXIT_FAILURE, "chdir($HOME)");
    }

    char* sh_argv[] = { "/usr/bin/sh", NULL };

    execvp(sh_argv[0], sh_argv);
    err(EXIT_FAILURE, "execvp");
}
