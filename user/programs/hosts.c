#include "hosts.h"
#include "unistd.h"

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_RDONLY 0u

static char lower_char(char value) {
    return (value >= 'A' && value <= 'Z') ? (char)(value + ('a' - 'A')) : value;
}

int rix_dns_valid_name(const char *name) {
    size_t length = 0, label = 0;
    if (!name || !name[0]) return -1;
    while (name[length]) {
        char c = name[length];
        if (c == '.') {
            if (!label || label > 63) return -1;
            label = 0;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_') {
            if (++label > 63) return -1;
        } else {
            return -1;
        }
        if (++length > 128) return -1;
    }
    if (!label) return -1;
    return 0;
}

static int parse_ipv4(const char *text, size_t length, uint32_t *address) {
    uint32_t parts[4] = {0, 0, 0, 0};
    int part = 0, digits = 0;
    size_t i = 0;
    if (!text || !length || !address) return -1;
    for (i = 0; i < length; ++i) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            if (++digits > 3) return -1;
            parts[part] = parts[part] * 10u + (uint32_t)(c - '0');
            if (parts[part] > 255u) return -1;
        } else if (c == '.') {
            if (!digits || part >= 3) return -1;
            ++part;
            digits = 0;
        } else {
            return -1;
        }
    }
    if (part != 3 || !digits) return -1;
    *address = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

int rix_parse_ipv4(const char *text, uint32_t *address) {
    size_t length = 0;
    if (!text || !address) return -1;
    while (text[length]) {
        if (++length > 15) return -1;
    }
    if (!length) return -1;
    return parse_ipv4(text, length, address);
}

static int token_is_name(const char *text, size_t length) {
    size_t i = 0;
    if (!text || !length || length > 128) return 0;
    for (i = 0; i < length; ++i) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return 0;
    }
    return 1;
}

static int names_equal(const char *entry, size_t entry_length, const char *query) {
    size_t query_length = 0, i = 0;
    if (!entry || !query) return 0;
    while (query[query_length]) ++query_length;
    if (query_length && query[query_length - 1] == '.') --query_length;
    if (!entry_length || entry_length != query_length) return 0;
    for (i = 0; i < entry_length; ++i)
        if (lower_char(entry[i]) != lower_char(query[i])) return 0;
    return 1;
}

static int blank_char(char c) { return c == ' ' || c == '\t'; }

int rix_hosts_parse(const char *text, size_t length, const char *name,
                    uint32_t *address) {
    size_t i = 0, query_length = 0;
    if (!text || !name || !address) return -1;
    while (name[query_length]) ++query_length;
    if (!query_length || query_length > 128) return -1;
    while (i < length) {
        size_t line = i, end, cursor;
        size_t ip_start, ip_end, name_start, name_end;
        uint32_t ip = 0;
        while (i < length && text[i] != '\n') ++i;
        end = i;
        if (i < length) ++i;
        if (end > line && text[end - 1] == '\r') --end;
        cursor = line;
        while (cursor < end && blank_char(text[cursor])) ++cursor;
        if (cursor >= end || text[cursor] == '#') continue;
        ip_start = cursor;
        while (cursor < end && !blank_char(text[cursor]) && text[cursor] != '#')
            ++cursor;
        ip_end = cursor;
        if (parse_ipv4(text + ip_start, ip_end - ip_start, &ip) != 0) continue;
        for (;;) {
            while (cursor < end && blank_char(text[cursor])) ++cursor;
            if (cursor >= end || text[cursor] == '#') break;
            name_start = cursor;
            while (cursor < end && !blank_char(text[cursor]) && text[cursor] != '#')
                ++cursor;
            name_end = cursor;
            if (token_is_name(text + name_start, name_end - name_start) &&
                names_equal(text + name_start, name_end - name_start, name)) {
                *address = ip;
                return 0;
            }
        }
    }
    return -1;
}

static int read_whole_file(const char *path, char *buffer, size_t capacity,
                           size_t *out_length) {
    int fd;
    size_t used = 0;
    if (!path || !buffer || !out_length || capacity < 2u) return -1;
    fd = openat(RIX_VFS_AT_FDCWD, path, RIX_VFS_O_RDONLY, 0u);
    if (fd < 0) return -1;
    for (;;) {
        rix_ssize_t count = read(fd, buffer + used, capacity - used - 1u);
        if (count < 0) {
            (void)close(fd);
            return -1;
        }
        if (count == 0) break;
        used += (size_t)count;
        if (used >= capacity - 1u) {
            (void)close(fd);
            return -1;
        }
    }
    buffer[used] = 0;
    *out_length = used;
    return close(fd);
}

int rix_hosts_lookup(const char *name, uint32_t *address) {
    static const char *paths[2] = {RIX_HOSTS_FILE, RIX_HOSTS_FALLBACK};
    char text[RIX_HOSTS_MAX_FILE];
    int k;
    if (!name || !address) return -1;
    for (k = 0; k < 2; ++k) {
        size_t length = 0;
        if (read_whole_file(paths[k], text, sizeof(text), &length) != 0) continue;
        if (rix_hosts_parse(text, length, name, address) == 0) return 0;
    }
    return -1;
}

static int keyword_is(const char *text, size_t length, const char *word) {
    size_t k = 0;
    if (!text || !word) return 0;
    while (word[k]) ++k;
    if (length != k) return 0;
    for (k = 0; k < length; ++k)
        if (lower_char(text[k]) != word[k]) return 0;
    return 1;
}

int rix_resolv_parse(const char *text, size_t length, uint32_t *server) {
    size_t i = 0;
    if (!text || !server) return -1;
    while (i < length) {
        size_t line = i, end, cursor;
        size_t key_start, key_end, value_start, value_end;
        uint32_t candidate = 0;
        while (i < length && text[i] != '\n') ++i;
        end = i;
        if (i < length) ++i;
        if (end > line && text[end - 1] == '\r') --end;
        cursor = line;
        while (cursor < end && blank_char(text[cursor])) ++cursor;
        if (cursor >= end || text[cursor] == '#') continue;
        key_start = cursor;
        while (cursor < end && !blank_char(text[cursor]) && text[cursor] != '#')
            ++cursor;
        key_end = cursor;
        if (!keyword_is(text + key_start, key_end - key_start, "nameserver"))
            continue;
        while (cursor < end && blank_char(text[cursor])) ++cursor;
        if (cursor >= end || text[cursor] == '#') continue;
        value_start = cursor;
        while (cursor < end && !blank_char(text[cursor]) && text[cursor] != '#')
            ++cursor;
        value_end = cursor;
        if (parse_ipv4(text + value_start, value_end - value_start,
                       &candidate) != 0)
            continue;
        *server = candidate;
        return 0;
    }
    return -1;
}

uint32_t rix_resolv_server(uint32_t fallback) {
    char text[RIX_RESOLV_MAX_FILE];
    size_t length = 0;
    uint32_t server = 0;
    if (read_whole_file(RIX_RESOLV_FILE, text, sizeof(text), &length) != 0)
        return fallback;
    if (rix_resolv_parse(text, length, &server) != 0) return fallback;
    return server;
}

static uint16_t get_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t get_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int dns_skip_name(const uint8_t *packet, size_t length, size_t *offset) {
    size_t cursor = *offset;
    while (cursor < length) {
        uint8_t label = packet[cursor++];
        if (!label) {
            *offset = cursor;
            return 0;
        }
        if ((label & 0xc0u) == 0xc0u) {
            if (cursor >= length) return -1;
            *offset = cursor + 1;
            return 0;
        }
        if (label > 63u || label > length - cursor) return -1;
        cursor += label;
    }
    return -1;
}

int rix_dns_query(const char *name, uint32_t server, uint32_t *address) {
    static uint8_t query[12 + 256 + 4];
    static uint16_t next_id = 0x1234u;
    uint16_t txid;
    size_t length = 0, i = 0;
    if (!name || !address || !server || rix_dns_valid_name(name) != 0) return -1;
    while (name[length]) ++length;
    if (name[length - 1] == '.') --length;
    txid = next_id++;
    if (!txid) txid = next_id++;
    query[0] = (uint8_t)(txid >> 8);
    query[1] = (uint8_t)txid;
    query[2] = 0x01;
    query[3] = 0x00;
    query[4] = 0x00;
    query[5] = 0x01;
    query[6] = 0x00;
    query[7] = 0x00;
    query[8] = 0x00;
    query[9] = 0x00;
    query[10] = 0x00;
    query[11] = 0x00;
    i = 12;
    {
        size_t start = 0, k = 0;
        for (k = 0; k <= length; ++k) {
            if (k == length || name[k] == '.') {
                size_t lab = k - start;
                if (!lab || lab > 63 || i + 1 + lab + 4 >= sizeof(query)) return -1;
                query[i++] = (uint8_t)lab;
                for (size_t m = 0; m < lab; ++m) query[i++] = (uint8_t)name[start + m];
                start = k + 1;
            }
        }
    }
    query[i++] = 0;
    query[i++] = 0;
    query[i++] = 1;
    query[i++] = 0;
    query[i++] = 1;
    {
        uint8_t response[512] = {0};
        int fd = socket_open(RIX_NET_SOCKET_UDP);
        rix_timespec_t pause = {0, 100000000u};
        unsigned round, attempt;
        if (fd < 0) return -1;
        for (round = 0; round < 40; ++round) {
            if (socket_send(fd, query, i,
                            (rix_net_endpoint_t){server, 53}) != (int)i)
                return -1;
            for (attempt = 0; attempt < 200; ++attempt) {
                int received = socket_receive(fd, response, sizeof(response), 0);
                if (received < 12 || get_be16(response) != txid) continue;
                {
                    uint16_t flags = get_be16(response + 2);
                    uint16_t answers = get_be16(response + 6);
                    if ((flags & 0x8000u) && !(flags & 0x000fu) && answers) {
                        size_t offset = 12;
                        if (dns_skip_name(response, (size_t)received, &offset) == 0 &&
                            offset + 4 <= (size_t)received) {
                            uint16_t answer;
                            offset += 4;
                            for (answer = 0; answer < answers; ++answer) {
                                uint16_t type, class_code, data_length;
                                if (dns_skip_name(response, (size_t)received,
                                                  &offset) != 0 ||
                                    offset + 10 > (size_t)received)
                                    break;
                                type = get_be16(response + offset);
                                class_code = get_be16(response + offset + 2);
                                data_length = get_be16(response + offset + 8);
                                offset += 10;
                                if (offset + data_length > (size_t)received) break;
                                if (type == 1 && class_code == 1 && data_length == 4) {
                                    *address = get_be32(response + offset);
                                    return 0;
                                }
                                offset += data_length;
                            }
                        }
                    }
                }
            }
            (void)nanosleep(&pause, NULL);
        }
    }
    return -1;
}
