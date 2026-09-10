#pragma once

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : __builtin_trap())
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif
