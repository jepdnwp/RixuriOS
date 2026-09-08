#pragma once
#include <stddef.h>
#include <stdint.h>

/* Shared userspace name-resolution helpers (freestanding-clean; only the
 * syscalls declared in user/libc/include/unistd.h are used).
 *
 * Resolution order is: /etc/hosts, then /etc/hostlist, then DNS.
 * The DNS server comes from the first valid 'nameserver <ipv4>' line of
 * /etc/resolv.conf, falling back to the caller-provided default.
 */
#define RIX_HOSTS_FILE "/etc/hosts"
#define RIX_HOSTS_FALLBACK "/etc/hostlist"
#define RIX_RESOLV_FILE "/etc/resolv.conf"
#define RIX_HOSTS_MAX_FILE 2048u
#define RIX_RESOLV_MAX_FILE 512u

int rix_dns_valid_name(const char *name);
int rix_parse_ipv4(const char *text, uint32_t *address);
int rix_hosts_parse(const char *text, size_t length, const char *name,
                    uint32_t *address);
int rix_hosts_lookup(const char *name, uint32_t *address);
int rix_resolv_parse(const char *text, size_t length, uint32_t *server);
uint32_t rix_resolv_server(uint32_t fallback);
int rix_dns_query(const char *name, uint32_t server, uint32_t *address);
