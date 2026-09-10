#pragma once

#define CHAR_BIT __CHAR_BIT__
#define SCHAR_MIN (-__SCHAR_MAX__ - 1)
#define SCHAR_MAX __SCHAR_MAX__
#define UCHAR_MAX (__SCHAR_MAX__ * 2 + 1)
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#define SHRT_MIN (-__SHRT_MAX__ - 1)
#define SHRT_MAX __SHRT_MAX__
#define USHRT_MAX (__SHRT_MAX__ * 2 + 1)
#define INT_MIN (-__INT_MAX__ - 1)
#define INT_MAX __INT_MAX__
#define UINT_MAX (__INT_MAX__ * 2U + 1U)
#define LONG_MIN (-__LONG_MAX__ - 1L)
#define LONG_MAX __LONG_MAX__
#define ULONG_MAX (__LONG_MAX__ * 2UL + 1UL)
#define LLONG_MIN (-__LONG_LONG_MAX__ - 1LL)
#define LLONG_MAX __LONG_LONG_MAX__
#define ULLONG_MAX (__LONG_LONG_MAX__ * 2ULL + 1ULL)
#ifndef PTRDIFF_MIN
#define PTRDIFF_MIN (-__PTRDIFF_MAX__ - 1)
#endif
#ifndef PTRDIFF_MAX
#define PTRDIFF_MAX __PTRDIFF_MAX__
#endif
#ifndef SIZE_MAX
#define SIZE_MAX __SIZE_MAX__
#endif
#define SSIZE_MAX __PTRDIFF_MAX__
#define MB_LEN_MAX 1
#define PATH_MAX 4096
#define FILENAME_MAX 256
