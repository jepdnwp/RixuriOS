#pragma once
#include <stdint.h>
#include <netinet/in.h>

/* IPv4 address conversion. Pure bounded helpers; no resolver.
 * inet_ntoa uses one static buffer (not reentrant); inet_ntop/inet_pton
 * cover AF_INET only and fail closed with NULL/0/-1 + errno. */

uint32_t inet_addr(const char *text);
int inet_pton(int family, const char *text, void *output);
const char *inet_ntop(int family, const void *source, char *output, uint32_t length);
char *inet_ntoa(struct in_addr address);
