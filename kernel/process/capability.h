#ifndef RIXURIOS_CAPABILITY_H
#define RIXURIOS_CAPABILITY_H

#include <stdint.h>

#define RIX_CAP_DAC_OVERRIDE (1ULL << 0)
#define RIX_CAP_SETUID       (1ULL << 1)
#define RIX_CAP_SETGID       (1ULL << 2)
#define RIX_CAP_KILL         (1ULL << 3)
#define RIX_CAP_TTY_ADMIN    (1ULL << 4)
#define RIX_CAP_ACL_ADMIN    (1ULL << 5)
#define RIX_CAP_SESSION_ADMIN (1ULL << 6)
#define RIX_CAP_AUDIT_ADMIN  (1ULL << 7)
#define RIX_CAP_DELEGATE     (1ULL << 8)
#define RIX_CAP_ALL (RIX_CAP_DAC_OVERRIDE | RIX_CAP_SETUID | RIX_CAP_SETGID | \
                    RIX_CAP_KILL | RIX_CAP_TTY_ADMIN | RIX_CAP_ACL_ADMIN | \
                    RIX_CAP_SESSION_ADMIN | RIX_CAP_AUDIT_ADMIN | RIX_CAP_DELEGATE)

int capability_valid(uint64_t mask);

#endif
