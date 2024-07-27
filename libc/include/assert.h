#ifndef _ASSERT_H
#define _ASSERT_H

#undef assert

#ifndef NDEBUG
void __assert_internal(const char*, int, const char*, const char*);
#define assert(statement) ((statement) ? (void) 0 : __assert_internal(__FILE__, __LINE__, __FUNCTION__, #statement))
#else
#define assert(statement) ((void) 0)
#endif

#endif /* _ASSERT_H */
