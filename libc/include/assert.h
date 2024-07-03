#ifndef _ASSERT_H
#define _ASSERT_H

#undef assert

#ifdef NDEBUG
#define assert(test) ((void) 0)
#else
// TODO: implement active form of assert
#define assert(test) ((void) 0)
#ednif

#endif /* _ASSERT_H */
