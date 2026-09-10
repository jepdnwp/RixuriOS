#pragma once

/* Minimal C-locale compatibility.
 *
 * Only the "C" (and alias "POSIX"/"") locale exists; the process starts
 * in it and stays in it. setlocale() accepts those spellings and returns
 * "C"; every other request returns NULL without changing anything.
 * localeconv() reports the fixed C numeric formatting. UTF-8 byte
 * handling lives in <wchar.h>; collation is raw memcmp order. */

#define LC_CTYPE 0
#define LC_NUMERIC 1
#define LC_TIME 2
#define LC_COLLATE 3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL 6

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
};

char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);
