#include "hosts.h"
#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) {
    size_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

static void out(const char *text) { (void)write(1, text, length(text)); }

static void out_decimal(uint32_t value) {
    char digits[10];
    size_t count = 0;
    if (!value) {
        out("0");
        return;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count) {
        char c = digits[--count];
        (void)write(1, &c, 1);
    }
}

static void out_address(uint32_t address) {
    out_decimal((address >> 24) & 0xffu);
    out(".");
    out_decimal((address >> 16) & 0xffu);
    out(".");
    out_decimal((address >> 8) & 0xffu);
    out(".");
    out_decimal(address & 0xffu);
}

int program_main(int argc, char **argv, char **envp) {
    uint32_t address = 0;
    (void)envp;
    if (argc != 2 || !argv[1] || rix_dns_valid_name(argv[1]) != 0) {
        out("host: expected one name\n");
        return 2;
    }
    if (rix_hosts_lookup(argv[1], &address) != 0 &&
        rix_dns_query(argv[1], rix_resolv_server(RIX_NET_DEVICE_DNS),
                      &address) != 0) {
        out("host: not found: ");
        out(argv[1]);
        out("\n");
        return 1;
    }
    out(argv[1]);
    out(" has address ");
    out_address(address);
    out("\n");
    return 0;
}
