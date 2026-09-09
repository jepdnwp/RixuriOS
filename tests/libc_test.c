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
static int compare_ints(const void *left, const void *right) { return *(const int *)left - *(const int *)right; }
void *sbrk(ptrdiff_t increment) {
    if (increment < 0 || (size_t)increment > sizeof(test_heap) - test_break) return (void *)-1;
    void *old = test_heap + test_break;
    test_break += (size_t)increment;
    return old;
}
rix_ssize_t read(int fd, void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; return -1; }
rix_ssize_t getrandom(void *buffer, size_t length, uint32_t flags) { static unsigned seed = 0; (void)flags; ++seed; for (size_t i = 0; i < length; ++i) ((unsigned char *)buffer)[i] = (unsigned char)(0xa5u ^ seed ^ (unsigned)i); return (rix_ssize_t)length; }
static size_t host_written;
rix_ssize_t write(int fd, const void *buffer, size_t count) { (void)buffer; if (fd == 1) host_written += count; return (rix_ssize_t)count; }
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
    FILE stream = { .fd = 1 };
    char io_buffer[32];
    assert(setvbuf(&stream, io_buffer, _IOFBF, sizeof(io_buffer)) == 0);
    assert(fputs("stdio", &stream) == 0);
    assert(fputc('!', &stream) == '!');
    assert(host_written == 0 && fflush(&stream) == 0 && host_written == 6);
    FILE formatted_stream = { .fd = 1 };
    assert(fprintf(&formatted_stream, "%s:%d", "fmt", 7) == 5);
    errno = RIX_EINVAL;
    assert(fprintf(&formatted_stream, "x") == 1);
    assert(strcmp(strerror(RIX_ENOENT), "No such file or directory") == 0);
    char error_text[8];
    assert(strerror_r(RIX_ENOENT, error_text, sizeof(error_text)) == RIX_ERANGE && error_text[7] == 0);
    assert(fgetc(&formatted_stream) == EOF && ferror(&formatted_stream) && !feof(&formatted_stream));
    clearerr(&formatted_stream);
    assert(!ferror(&formatted_stream) && !feof(&formatted_stream));
    char *end = 0;
    assert(atoi("-42") == -42);
    assert(strtol("0x2a", &end, 0) == 42 && *end == 0);
    assert(strtoul("101", &end, 2) == 5 && *end == 0);
    int values[5] = { 4, 1, 5, 2, 3 };
    qsort(values, 5, sizeof(values[0]), compare_ints);
    for (int i = 0; i < 5; ++i) assert(values[i] == i + 1);
    assert(abs(-7) == 7 && labs(-9L) == 9L);
    assert(setenv("RIX_TEST", "one", 1) == 0 && strcmp(getenv("RIX_TEST"), "one") == 0);
    assert(setenv("RIX_TEST", "two", 0) == 0 && strcmp(getenv("RIX_TEST"), "one") == 0);
    assert(setenv("RIX_TEST", "two", 1) == 0 && strcmp(getenv("RIX_TEST"), "two") == 0);
    assert(unsetenv("RIX_TEST") == 0 && getenv("RIX_TEST") == 0);
    char putenv_value[] = "RIX_PUT=ok";
    assert(putenv(putenv_value) == 0 && strcmp(getenv("RIX_PUT"), "ok") == 0);
    assert(clearenv() == 0 && getenv("RIX_PUT") == 0);
    srand(1234u); int first = rand(); srand(1234u); assert(rand() == first);
    assert(arc4random() != arc4random());
    return 0;
}
