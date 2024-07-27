#include <stdbool.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/wait.h>
#include <unistd.h> 

#define ARRAY_SIZE(xs) (sizeof((xs)) / sizeof((xs)[0]))

struct shell_builtin {
    const char* name;
    bool (*func) (char**);
};

static bool builtin_cd(char** args) {
    if (args[1] == NULL) {
        fputs("sh: usage: cd [DIR]\n", stderr);
    } else {
        if (chdir(args[1]) != 0) {
            perror("cd");
        }
    }

    return false;
}

static bool builtin_exit(char** args) {
    (void) args;
    return true;
}

static bool builtin_pwd(char** args) {
    (void) args;

    char* buf = malloc(4096);
    if (buf == NULL) {
        perror("pwd");
    } else {
        if (getcwd(buf, 4096) == NULL) {
            perror("pwd");
        }

        puts(buf);
        putchar('\n');
        fflush(stdout);
    }

    free(buf);
    return false;
}

static struct shell_builtin builtins[] = {
    { "cd", builtin_cd },
    { "exit", builtin_exit },
    { "pwd", builtin_pwd },
};

char* read_line(void) {
    int bufsize = 256;
    int position = 0;

    char* buf = malloc(sizeof(char) * bufsize);
    if (!buf) {
        fputs("failed to allocate memory for line buffer\n", stderr);
        exit(EXIT_FAILURE);
    }

    int c;
    while (1) {
        c = getchar();

        if (c == EOF || c == '\n') {
            buf[position] = '\0';
            return buf;
        } else {
            buf[position] = c;
        }

        position++;

        if (position >= bufsize) {
            bufsize += 256;
            buf = realloc(buf, bufsize);
            if (!buf) {
                fputs("failed to allocate memory for line buffer\n", stderr);
                exit(EXIT_FAILURE);
            }
        }
    }
}

static char** split_args(char *line) {
    int bufsize = 64;
    int position = 0;

    char** tokens = malloc(bufsize * sizeof(char*));
    if (tokens == NULL) {
        fputs("unable to allocate memory for command args\n", stderr);
        exit(EXIT_FAILURE);
    }

    char* token = strtok(line, " \t\n\a");
    while (token != NULL) {
        tokens[position] = token;
        position++;
        token = strtok(NULL, " \t\n\a");
    }

    tokens[position] = NULL;
    return tokens;
}

static void run_program(char** args) {
    pid_t pid = fork();
    if (pid == 0) {
        if (execv(args[0], args) == -1) {
            perror("execv");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("fork");
    } else {
        waitpid(pid, NULL, 0);
    }
}

static bool execute(char** args) {
    if (args[0] == NULL) {
        return false;
    }

    for (size_t i = 0; i < ARRAY_SIZE(builtins); i++) {
        if (!strcmp(args[0], builtins[i].name)) {
            return (*builtins[i].func)(args);
        }
    }

    run_program(args);
    return false;
}

int main(void) {
    char* line;
    char** args;
    bool do_quit = false;

    do {
        fputs("> ", stdout);
        fflush(stdout);

        line = read_line();
        args = split_args(line);
        do_quit = execute(args);

        free(line);
        free(args);
    } while (!do_quit);

    return EXIT_SUCCESS;
}
