#ifndef _WCTYPE_H
#define _WCTYPE_H

#include <stddef.h>
#include <wchar.h>

#define WEOF __WINT_MAX__

typedef wint_t (*wctrans_t)(wint_t);
typedef int (*wctype_t)(wint_t);

// TODO: implement wctype.h
int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswctype(wint_t wc, wctype_t charclass);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);
wint_t towctrans(wint_t ch, wctrans_t desc);
wint_t towlower(wint_t ch);
wint_t towupper(wint_t ch);

#endif /* _WCTYPE_H */
