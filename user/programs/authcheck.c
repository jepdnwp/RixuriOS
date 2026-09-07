#include "unistd.h"
#include "auth_crypto.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_EACCES 13
#define RIX_EINVAL 22
#define RIX_VFS_O_RDONLY 0u
#define RIX_AUTH_MAX_FILE 4096u
#define RIX_AUTH_MAX_SALT 32u
#define RIX_AUTH_HASH_HEX 64u
#define RIX_AUTH_MAX_ROUNDS 1024u

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static void out(const char *text) {
    (void)write(1, text, text_length(text));
}

static int text_equal(const char *left, const char *right) {
    size_t index = 0;
    if (!left || !right) return 0;
    while (left[index] && right[index] && left[index] == right[index]) ++index;
    return left[index] == 0 && right[index] == 0;
}

static int read_file(const char *path, char *buffer, size_t capacity, size_t *out_length) {
    int fd;
    size_t used = 0;
    if (!buffer || !out_length || capacity < 2u) return -1;
    fd = openat(RIX_VFS_AT_FDCWD, path, RIX_VFS_O_RDONLY, 0u);
    if (fd < 0) return fd;
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

static int next_field(const char **cursor, char *field, size_t capacity) {
    const char *start;
    size_t length = 0;
    if (!cursor || !*cursor || !field || capacity == 0) return -1;
    start = *cursor;
    while (start[length] && start[length] != ':' && start[length] != '\n') ++length;
    if (length + 1u > capacity) return -1;
    for (size_t index = 0; index < length; ++index) field[index] = start[index];
    field[length] = 0;
    if (start[length] == ':') *cursor = start + length + 1u;
    else *cursor = start + length;
    return (int)length;
}

static int parse_uint(const char *text, uint32_t *out_value) {
    uint32_t value = 0;
    size_t index = 0;
    if (!text || !text[0] || !out_value) return -1;
    while (text[index]) {
        uint32_t digit;
        if (text[index] < '0' || text[index] > '9') return -1;
        digit = (uint32_t)(text[index] - '0');
        if (value > (UINT32_MAX - digit) / 10u) return -1;
        value = value * 10u + digit;
        ++index;
    }
    *out_value = value;
    return 0;
}

static int find_passwd_account(const char *database, const char *wanted,
                               uint32_t *out_uid, uint32_t *out_gid) {
    const char *cursor = database;
    char name[64], uid[16], gid[16], home[256], shell[256];
    if (!database || !wanted || !out_uid || !out_gid) return -1;
    while (*cursor) {
        if (next_field(&cursor, name, sizeof(name)) < 0) return -1;
        if (next_field(&cursor, uid, sizeof(uid)) < 0 ||
            next_field(&cursor, gid, sizeof(gid)) < 0 ||
            next_field(&cursor, home, sizeof(home)) < 0 ||
            next_field(&cursor, shell, sizeof(shell)) < 0)
            return -1;
        if (text_equal(name, wanted)) {
            if (parse_uint(uid, out_uid) != 0 || parse_uint(gid, out_gid) != 0) return -1;
            return 0;
        }
        while (*cursor && *cursor != '\n') ++cursor;
        if (*cursor == '\n') ++cursor;
    }
    return -1;
}

static int decode_hex(const char *text, uint8_t output[32]) {
    for (unsigned index = 0; index < 32u; ++index) {
        unsigned high, low;
        char high_char = text[index * 2u];
        char low_char = text[index * 2u + 1u];
        if (high_char >= '0' && high_char <= '9') high = (unsigned)(high_char - '0');
        else if (high_char >= 'a' && high_char <= 'f') high = (unsigned)(high_char - 'a' + 10);
        else return -1;
        if (low_char >= '0' && low_char <= '9') low = (unsigned)(low_char - '0');
        else if (low_char >= 'a' && low_char <= 'f') low = (unsigned)(low_char - 'a' + 10);
        else return -1;
        output[index] = (uint8_t)((high << 4u) | low);
    }
    return 0;
}

static int shadow_digest(const char *database, const char *wanted,
                         const char *password, uint8_t output[32]) {
    const char *cursor = database;
    char name[64], algorithm[32], salt[33], rounds_text[16], hash[65];
    uint32_t rounds;
    uint8_t expected[32];
    if (!database || !wanted || !password || !output) return -1;
    while (*cursor) {
        if (next_field(&cursor, name, sizeof(name)) < 0) return -1;
        if (next_field(&cursor, algorithm, sizeof(algorithm)) < 0 ||
            next_field(&cursor, salt, sizeof(salt)) < 0 ||
            next_field(&cursor, rounds_text, sizeof(rounds_text)) < 0 ||
            next_field(&cursor, hash, sizeof(hash)) < 0)
            return -1;
        if (text_equal(name, wanted)) {
            if (!text_equal(algorithm, "rixsha256") || text_length(salt) != RIX_AUTH_MAX_SALT ||
                parse_uint(rounds_text, &rounds) != 0 || rounds == 0 || rounds > RIX_AUTH_MAX_ROUNDS ||
                text_length(hash) != RIX_AUTH_HASH_HEX || decode_hex(hash, expected) != 0)
                return -1;
            rix_password_digest(salt, password, rounds, output);
            return rix_constant_digest_equal(output, expected) ? 0 : 1;
        }
        while (*cursor && *cursor != '\n') ++cursor;
        if (*cursor == '\n') ++cursor;
    }
    return -1;
}

static int account_count(const char *database, uint32_t *out_count) {
    uint32_t count = 0;
    const char *cursor = database;
    if (!database || !out_count) return -1;
    while (*cursor) {
        if (*cursor != '\n') {
            ++count;
            while (*cursor && *cursor != '\n') ++cursor;
        }
        if (*cursor == '\n') ++cursor;
    }
    *out_count = count;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    char passwd[RIX_AUTH_MAX_FILE];
    char shadow[RIX_AUTH_MAX_FILE];
    size_t passwd_length, shadow_length;
    uint32_t uid, gid, count;
    (void)envp;
    if (read_file("/etc/passwd", passwd, sizeof(passwd), &passwd_length) != 0 ||
        read_file("/etc/shadow", shadow, sizeof(shadow), &shadow_length) != 0)
        return 1;
    (void)passwd_length;
    (void)shadow_length;
    if (argc == 2 && text_equal(argv[1], "list")) {
        if (account_count(passwd, &count) != 0) return 1;
        out("accounts=3\n");
        return count == 3u ? 0 : 1;
    }
    if (argc == 3 && text_equal(argv[1], "record")) {
        if (find_passwd_account(passwd, argv[2], &uid, &gid) != 0) return 1;
        if (!text_equal(argv[2], "operator") || uid != 1000u || gid != 1000u) return 1;
        out("account-record=PASS\n");
        return 0;
    }
    if (argc == 4 && text_equal(argv[1], "verify")) {
        uint8_t digest[32];
        if (find_passwd_account(passwd, argv[2], &uid, &gid) != 0 ||
            shadow_digest(shadow, argv[2], argv[3], digest) != 0) {
            out("auth-denied\n");
            return 1;
        }
        out("auth-pass\n");
        return 0;
    }
    if (argc == 2 && text_equal(argv[1], "protected")) {
        int fd;
        if (setuid(1000u) != 0) return 1;
        fd = openat(RIX_VFS_AT_FDCWD, "/etc/shadow", RIX_VFS_O_RDONLY, 0u);
        if (fd >= 0) {
            (void)close(fd);
            return 1;
        }
        if (fd != -RIX_EACCES) return 1;
        out("shadow-protected=PASS\n");
        return 0;
    }
    out("authcheck: expected list, record, verify or protected\n");
    return 2;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
