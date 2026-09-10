#pragma once
#include <stddef.h>

/* Bounded UTF-8 / wide-character compatibility.
 *
 * Encoding is always UTF-8; there is no shift state (mbstate_t is a
 * placeholder for the spelling). Conversions validate strictly per
 * RFC 3629: overlong forms, surrogate halves and code points above
 * U+10FFFF are rejected with errno EILSEQ... which has no Rix code yet,
 * so EINVAL is reported instead (documented in PHASE22_COMPAT.md).
 * All functions bound their inputs; mbstowcs/wcstombs never write past
 * the caller capacity including the terminator. */

#define WEOF ((wint_t)-1)

typedef int mbstate_t;

size_t mbrlen(const char *text, size_t length, mbstate_t *state);
size_t mbrtowc(wchar_t *output, const char *text, size_t length, mbstate_t *state);
size_t wcrtomb(char *output, wchar_t value, mbstate_t *state);
size_t mbstowcs(wchar_t *output, const char *text, size_t capacity);
size_t wcstombs(char *output, const wchar_t *text, size_t capacity);
size_t wcslen(const wchar_t *text);
