#ifndef _SH_BUILTINS_H
#define _SH_BUILTINS_H

struct shell_builtin {
    const char* name;
    int (*func) (int, char* []);
};

extern const struct shell_builtin BUILTINS[];

#endif /* _SH_BUILTINS_H */
