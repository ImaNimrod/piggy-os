#ifndef _SH_HISTORY_H
#define _SH_HISTORY_H

#define HISTORY_MAX_LENGTH 100

const char* history_next(void);
const char* history_prev(void);
void history_print(size_t count);
void history_push(const char* line);

#endif /* _SH_HISTORY_H */
