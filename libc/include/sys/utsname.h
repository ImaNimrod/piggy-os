#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

struct utsname {
    char sysname[64];
    char nodename[100];
    char release[64];
    char version[64];
    char machine[64];
};

int utsname(struct utsname*);

#endif /* _SYS_UTSNAME_H */
