#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static inline void hang(void) {
    for (;;) {
        __asm__ volatile("pause");
    }
}

static void print_motd(void) {
    int motd = open("/etc/motd", O_RDONLY);
    if (motd < 0) {
        puts("failed to open motd\n");
        hang();
    }

    off_t motd_size = lseek(motd, 0, SEEK_END);
    lseek(motd, 0, SEEK_SET);

    char buf[motd_size + 1];
    if (read(motd, buf, motd_size) != motd_size) {
        puts("failed to get motd size\n");
        hang();
    }

    write(STDOUT_FILENO, buf, motd_size);
}

int main(void) {
    print_motd();

    pid_t pid = fork();

    if (pid == 0) {
         char* const argv[] = {
            "/bin/sh",
            NULL,
        };

        char* const envp[] = {
            NULL,
        };

        execve(argv[0], argv, envp);
    } else if (pid < 0) {
        puts("failed to fork\n");
    } else {
        waitpid(pid, NULL, 0);
    }

    hang();
    return EXIT_SUCCESS;
}
