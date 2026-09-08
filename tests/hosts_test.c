#include "user/programs/hosts.h"
#include "unistd.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* In-memory fixture files for the openat/read/close syscalls used by
 * user/programs/hosts.c. A NULL fixture means the file does not exist. */
static const char *stub_hosts_content = 0;
static const char *stub_hostlist_content = 0;
static const char *stub_resolv_content = 0;
static const char *stub_cursor[16] = {0};
static size_t stub_read_chunk = 0;

int openat(int dirfd, const char *path, uint32_t flags, uint32_t mode) {
    (void)dirfd;
    (void)flags;
    (void)mode;
    if (!path) return -1;
    if (strcmp(path, "/etc/hosts") == 0 && stub_hosts_content) {
        stub_cursor[10] = stub_hosts_content;
        return 10;
    }
    if (strcmp(path, "/etc/hostlist") == 0 && stub_hostlist_content) {
        stub_cursor[11] = stub_hostlist_content;
        return 11;
    }
    if (strcmp(path, "/etc/resolv.conf") == 0 && stub_resolv_content) {
        stub_cursor[12] = stub_resolv_content;
        return 12;
    }
    return -1;
}

rix_ssize_t read(int fd, void *buf, size_t count) {
    size_t remaining, want;
    if (fd < 0 || fd >= 16 || !stub_cursor[fd] || !buf || !count) {
        if (fd >= 0 && fd < 16 && stub_cursor[fd] && buf) return 0;
        return -1;
    }
    remaining = strlen(stub_cursor[fd]);
    if (!remaining) return 0;
    want = remaining;
    if (stub_read_chunk && want > stub_read_chunk) want = stub_read_chunk;
    if (want > count) want = count;
    memcpy(buf, stub_cursor[fd], want);
    stub_cursor[fd] += want;
    return (rix_ssize_t)want;
}

int close(int fd) {
    if (fd < 0 || fd >= 16) return -1;
    stub_cursor[fd] = 0;
    return 0;
}

/* Network syscalls are unavailable on the host; rix_dns_query is
 * exercised on QEMU instead. The stubs fail closed like no-network. */
int socket_open(int type) {
    (void)type;
    return -1;
}

int socket_send(int fd, const void *data, size_t length,
                rix_net_endpoint_t destination) {
    (void)fd;
    (void)data;
    (void)length;
    (void)destination;
    return -1;
}

int socket_receive(int fd, void *data, size_t capacity,
                   rix_net_endpoint_t *source) {
    (void)fd;
    (void)data;
    (void)capacity;
    (void)source;
    return -1;
}

int nanosleep(const rix_timespec_t *request, rix_timespec_t *remaining) {
    (void)request;
    (void)remaining;
    return -1;
}

int main(void) {
    uint32_t address = 0;
    static const char basic[] = "127.0.0.1 localhost\n";
    assert(rix_hosts_parse(basic, sizeof(basic) - 1, "localhost", &address) == 0);
    assert(address == 0x7f000001u);

    static const char table[] = "# comment line\n"
                                "\n"
                                "10.0.2.2\tgateway qemu-gateway # trailing comment\n"
                                "10.0.2.3 nameserver\r\n"
                                "999.1.1.1 bogus skipped\n"
                                "10.9.9.9\n"
                                "10.0.2.4 multi one two\n";
    assert(rix_hosts_parse(table, sizeof(table) - 1, "gateway", &address) == 0);
    assert(address == 0x0a000202u);
    assert(rix_hosts_parse(table, sizeof(table) - 1, "QEMU-GATEWAY", &address) == 0);
    assert(address == 0x0a000202u);
    assert(rix_hosts_parse(table, sizeof(table) - 1, "nameserver", &address) == 0);
    assert(address == 0x0a000203u);
    assert(rix_hosts_parse(table, sizeof(table) - 1, "two", &address) == 0);
    assert(address == 0x0a000204u);
    assert(rix_hosts_parse(table, sizeof(table) - 1, "missing", &address) != 0);
    assert(rix_hosts_parse(table, sizeof(table) - 1, "bogus", &address) != 0);
    assert(rix_hosts_parse(table, sizeof(table) - 1, "localhost.", &address) != 0);
    assert(rix_hosts_parse("", 0, "localhost", &address) != 0);
    assert(rix_hosts_parse(basic, sizeof(basic) - 1, 0, &address) != 0);
    assert(rix_hosts_parse(basic, sizeof(basic) - 1, "localhost", 0) != 0);
    assert(rix_hosts_parse(0, 10, "localhost", &address) != 0);

    static const char dupes[] = "10.1.1.1 dup\n10.2.2.2 dup\n";
    assert(rix_hosts_parse(dupes, sizeof(dupes) - 1, "dup", &address) == 0);
    assert(address == 0x0a010101u);

    assert(rix_dns_valid_name("google.com") == 0);
    assert(rix_dns_valid_name("LOCALHOST") == 0);
    assert(rix_dns_valid_name("") != 0);
    assert(rix_dns_valid_name("bad name") != 0);
    assert(rix_dns_valid_name("trailing.") != 0);

    assert(rix_parse_ipv4("127.0.0.1", &address) == 0);
    assert(address == 0x7f000001u);
    assert(rix_parse_ipv4("10.0.2.2", &address) == 0);
    assert(address == 0x0a000202u);
    assert(rix_parse_ipv4("255.255.255.255", &address) == 0);
    assert(address == 0xffffffffu);
    assert(rix_parse_ipv4("0.0.0.0", &address) == 0);
    assert(address == 0u);
    assert(rix_parse_ipv4("256.1.1.1", &address) != 0);
    assert(rix_parse_ipv4("1.2.3", &address) != 0);
    assert(rix_parse_ipv4("1.2.3.4.5", &address) != 0);
    assert(rix_parse_ipv4("1.2.3.04x", &address) != 0);
    assert(rix_parse_ipv4("", &address) != 0);
    assert(rix_parse_ipv4("localhost", &address) != 0);
    assert(rix_parse_ipv4(0, &address) != 0);
    assert(rix_parse_ipv4("1.2.3.4", 0) != 0);

    stub_hosts_content = basic;
    stub_hostlist_content = 0;
    stub_read_chunk = 0;
    assert(rix_hosts_lookup("localhost", &address) == 0);
    assert(address == 0x7f000001u);
    assert(rix_hosts_lookup("absent", &address) != 0);
    stub_read_chunk = 1;
    assert(rix_hosts_lookup("localhost", &address) == 0);
    assert(address == 0x7f000001u);
    stub_read_chunk = 0;

    stub_hosts_content = 0;
    stub_hostlist_content = "10.5.5.5 fallback-name\n";
    assert(rix_hosts_lookup("fallback-name", &address) == 0);
    assert(address == 0x0a050505u);
    stub_hosts_content = "10.6.6.6 both\n";
    stub_hostlist_content = "10.7.7.7 both\n";
    assert(rix_hosts_lookup("both", &address) == 0);
    assert(address == 0x0a060606u);
    stub_hosts_content = 0;
    stub_hostlist_content = 0;
    assert(rix_hosts_lookup("both", &address) != 0);
    assert(rix_hosts_lookup(0, &address) != 0);

    static const char resolv[] = "# resolver config\n"
                                 "   nameserver 10.0.2.3  \n"
                                 "nameserver 1.1.1.1\n";
    uint32_t server = 0;
    assert(rix_resolv_parse(resolv, sizeof(resolv) - 1, &server) == 0);
    assert(server == 0x0a000203u);
    static const char resolv_bad[] = "# only comments\n"
                                     "nameserver not-an-ip\n"
                                     "search example.com\n";
    assert(rix_resolv_parse(resolv_bad, sizeof(resolv_bad) - 1, &server) != 0);
    assert(rix_resolv_parse("", 0, &server) != 0);
    assert(rix_resolv_parse(resolv, sizeof(resolv) - 1, 0) != 0);

    stub_resolv_content = resolv;
    assert(rix_resolv_server(0x08080808u) == 0x0a000203u);
    stub_resolv_content = resolv_bad;
    assert(rix_resolv_server(0x08080808u) == 0x08080808u);
    stub_resolv_content = 0;
    assert(rix_resolv_server(0x08080808u) == 0x08080808u);

    return 0;
}
