#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>

static unsigned char test_heap[128u * 1024u];
static size_t test_break;
void *sbrk(ptrdiff_t increment) {
    if (increment < 0 || (size_t)increment > sizeof(test_heap) - test_break) return (void *)-1;
    void *old = test_heap + test_break;
    test_break += (size_t)increment;
    return old;
}
rix_ssize_t read(int fd, void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; return -1; }
rix_ssize_t write(int fd, const void *buffer, size_t count) { (void)fd; (void)buffer; return (rix_ssize_t)count; }
int open(const char *path, uint32_t flags, ...) { (void)path; (void)flags; return -1; }
int close(int fd) { (void)fd; return 0; }
int getdents(int fd, rix_dirent_t *entries, size_t capacity, size_t *count) { (void)fd; (void)entries; (void)capacity; if (count) *count = 0; return -1; }
off_t lseek(int fd, off_t offset, int whence) { (void)fd; (void)whence; return offset; }

int main(void) {
    char source[] = "rixurios";
    char buffer[32];
    assert(strlen(source) == 8);
    assert(strcmp(source, "rixurios") == 0);
    assert(strncmp(source, "rix", 3) == 0);
    assert(strchr(source, 'u') == source + 3);
    assert(memcpy(buffer, source, sizeof(source)) == buffer);
    assert(memcmp(buffer, source, sizeof(source)) == 0);
    memmove(buffer + 2, buffer, 6);
    assert(buffer[2] == 'r' && buffer[7] == 'i');
    memset(buffer, 'x', 4);
    assert(buffer[0] == 'x' && buffer[3] == 'x');
    void *a = malloc(32);
    assert(a != 0);
    void *b = calloc(4, 8);
    assert(b != 0);
    for (int i = 0; i < 32; ++i) assert(((unsigned char *)b)[i] == 0);
    a = realloc(a, 64);
    assert(a != 0);
    free(a); free(b);
    assert(malloc((size_t)-1) == 0 && errno == RIX_ENOMEM);
    char formatted[32];
    assert(isalpha('R') && isdigit('7') && isspace('\n') && toupper('a') == 'A' && tolower('Z') == 'z');
    assert(snprintf(formatted, sizeof(formatted), "%s:%d:%x:%c", "ok", -12, 0xbeef, '!') == 13);
    assert(strcmp(formatted, "ok:-12:beef:!") == 0);
    assert(snprintf(formatted, 5, "%s", "abcdef") == 6 && strcmp(formatted, "abcd") == 0);
    struct dirent entry = {0};
    entry.d_ino = 7;
    assert(entry.d_ino == 7 && AT_FDCWD == -100 && O_CREAT == 4u);
    assert(S_ISDIR(S_IFDIR) && S_ISREG(S_IFREG));
    return 0;
}
