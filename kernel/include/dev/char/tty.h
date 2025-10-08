#ifndef _KERNEL_DEV_TTY_H
#define _KERNEL_DEV_TTY_H 1

#include <stdbool.h>
#include <stddef.h>

#define TTY_DEV_MAJOR 2
#define TTY_DEV_MINOR 0

#define TCGETS		0x5401
#define TCSETS		0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ	0x5413
#define TIOCSWINSZ	0x5414

extern bool tty_is_ready; 

void tty_add_chars(const char* chars, size_t count);
void tty_init(void);

#endif /* _KERNEL_DEV_TTY_H */
