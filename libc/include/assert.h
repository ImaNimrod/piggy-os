#ifndef _ASSERT_H
#define _ASSERT_H

#undef assert

#if !defined(NDEBUG)
#define assert(c)
#else
#define assert(c)
#endif

// TODO: implement assert

#endif /* _ASSERT_H */
