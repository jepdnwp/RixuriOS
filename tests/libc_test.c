#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <locale.h>
#include <wchar.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
static_assert(sizeof(uint32_t) == 4, "uint32_t width");

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
int clock_gettime(clockid_t clock, struct timespec *out) { (void)clock; if (!out) return -1; out->tv_sec = 1700000000; out->tv_nsec = 0; return 0; }
rix_pid_t getpid(void) { return 42; }
static int once_counter;
static void once_function(void) { ++once_counter; }
static void noop_handler(void) {}

int main(void) {
    bool headers_ok = true;
    assert(headers_ok && INT_MAX > 0 && UINT_MAX > INT_MAX && SIZE_MAX > 0 && PATH_MAX >= 256);
    struct header_probe { char tag; uint32_t value; };
    assert(sizeof(uint64_t) == 8 && UINTPTR_MAX >= UINT32_MAX && offsetof(struct header_probe, value) >= 1);
    char source[] = "rixurios";
    char buffer[32];
    assert(strlen(source) == 8);
    assert(strcmp(source, "rixurios") == 0);
    assert(strncmp(source, "rix", 3) == 0);
    assert(strchr(source, 'u') == source + 3);
    assert(memchr(source, 'u', sizeof(source)) == source + 3);
    assert(strrchr(source, 'i') == source + 5);
    assert(strstr(source, "uri") == source + 3);
    assert(strspn("123abc", "123") == 3 && strcspn("abc123", "123") == 3);
    assert(*strpbrk("rixurios", "xz") == 'x' && *strpbrk("rixurios", "uo") == 'u');
    char tokens[] = "one,,two three";
    assert(strcmp(strtok(tokens, ", "), "one") == 0);
    assert(strcmp(strtok(0, ", "), "two") == 0);
    assert(strcmp(strtok(0, ", "), "three") == 0 && strtok(0, ", ") == 0);
    char strings[32] = "rix";
    assert(strcmp(strcpy(strings, "rixuri"), "rixuri") == 0);
    assert(strncpy(strings, "os", 5) == strings && strings[2] == 0);
    strcpy(strings, "rix"); assert(strcmp(strcat(strings, "uri"), "rixuri") == 0);
    strcpy(strings, "rix"); assert(strcmp(strncat(strings, "urios", 2), "rixur") == 0);
    char *duplicate = strdup("duplicate"); assert(duplicate && strcmp(duplicate, "duplicate") == 0); free(duplicate);
    duplicate = strndup("truncate", 4); assert(duplicate && strcmp(duplicate, "trun") == 0); free(duplicate);
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
    assert(isalpha('R') && isdigit('7') && isblank('\t') && iscntrl('\n') && isgraph('!'));
    assert(isprint(' ') && ispunct('!') && isspace('\n') && isxdigit('f') && !isxdigit('g'));
    assert(toupper('a') == 'A' && tolower('Z') == 'z');
    assert(snprintf(formatted, sizeof(formatted), "%s:%d:%x:%c", "ok", -12, 0xbeef, '!') == 13);
    assert(strcmp(formatted, "ok:-12:beef:!") == 0);
    assert(snprintf(formatted, 5, "%s", "abcdef") == 6 && strcmp(formatted, "abcd") == 0);
    assert(snprintf(formatted, sizeof(formatted), "%08x", 0xbeef) == 8 && strcmp(formatted, "0000beef") == 0);
    assert(snprintf(formatted, sizeof(formatted), "%-6s", "ok") == 6 && strcmp(formatted, "ok    ") == 0);
    assert(snprintf(formatted, sizeof(formatted), "%i:%o:%X:%%", -7, 012u, 0xbeefu) == 12 && strcmp(formatted, "-7:12:BEEF:%") == 0);
    assert(snprintf(formatted, sizeof(formatted), "%#x %#X %#o", 0xbeefu, 0xbeefu, 012u) == 17 && strcmp(formatted, "0xbeef 0XBEEF 012") == 0);
    assert(snprintf(formatted, sizeof(formatted), "%p", (void *)(uintptr_t)0x1234u) == 6 && strcmp(formatted, "0x1234") == 0);
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
    assert(ungetc('Q', &formatted_stream) == 'Q' && fgetc(&formatted_stream) == 'Q');
    assert(ungetc(EOF, &formatted_stream) == EOF);
    assert(fileno(&formatted_stream) == 1);
    errno = 0;
    assert(remove(0) == -1 && errno == RIX_EINVAL);
    char *end = 0;
    assert(atoi("-42") == -42);
    time_t now = time(0); struct tm calendar;
    assert(now == 1700000000 && gmtime_r(&(time_t){0}, &calendar) && calendar.tm_year == 70 && calendar.tm_mon == 0 && calendar.tm_mday == 1);
    assert(mktime(&calendar) == 0 && difftime(10, 3) == 7.0);
    assert(strtol("0x2a", &end, 0) == 42 && *end == 0);
    assert(strtoul("101", &end, 2) == 5 && *end == 0);
    int values[5] = { 4, 1, 5, 2, 3 };
    qsort(values, 5, sizeof(values[0]), compare_ints);
    for (int i = 0; i < 5; ++i) assert(values[i] == i + 1);
    int needle = 3;
    assert(*(int *)bsearch(&needle, values, 5, sizeof(values[0]), compare_ints) == 3);
    needle = 9;
    assert(bsearch(&needle, values, 5, sizeof(values[0]), compare_ints) == 0);
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
    assert(strnlen("hello", 10) == 5 && strnlen("hello", 3) == 3 && strnlen(0, 4) == 0);
    char rtok[] = "a:b::c"; char *save = 0;
    assert(strcmp(strtok_r(rtok, ":", &save), "a") == 0);
    assert(strcmp(strtok_r(0, ":", &save), "b") == 0);
    assert(strcmp(strtok_r(0, ":", &save), "c") == 0 && strtok_r(0, ":", &save) == 0);
    char cc[8]; assert(memccpy(cc, "abcde", 'c', sizeof(cc)) == cc + 3 && cc[2] == 'c');
    assert(memccpy(cc, "abc", 'z', 3) == 0);
    int di = 0, dn = 0; unsigned du = 0; char ds[16]; char dc = 0;
    assert(sscanf("  -42 0x2a hello Z", "%d %x %s %c%n", &di, &du, ds, &dc, &dn) == 4);
    assert(di == -42 && du == 42u && strcmp(ds, "hello") == 0 && dc == 'Z' && dn > 0);
    int sup = 0;
    assert(sscanf("12:34", "%*d:%d", &sup) == 1 && sup == 34);
    assert(sscanf("abc", "%d", &sup) == EOF || sscanf("abc", "%d", &sup) == 0);
    assert(sscanf("100%", "%d%%", &sup) == 1 && sup == 100);
    assert(setlocale(LC_ALL, 0) && strcmp(setlocale(LC_ALL, "C"), "C") == 0);
    assert(setlocale(LC_ALL, "POSIX") && setlocale(LC_ALL, "") && !setlocale(LC_ALL, "xx_YY"));
    assert(localeconv() && strcmp(localeconv()->decimal_point, ".") == 0);
    wchar_t wc = 0; mbstate_t mbs = 0;
    assert(mbrtowc(&wc, "A", 1, &mbs) == 1 && wc == L'A');
    assert(mbrtowc(&wc, "\xc3\xa9", 2, &mbs) == 2 && wc == 0xe9);
    assert(mbrtowc(&wc, "\xff", 1, &mbs) == (size_t)-1 && mbrtowc(&wc, "\xc3", 1, &mbs) == (size_t)-1);
    char mb[8]; assert(wcrtomb(mb, 0xe9, &mbs) == 2 && !(memcmp(mb, "\xc3\xa9", 2)));
    wchar_t ws[8]; assert(mbstowcs(ws, "hi\xc3\xa9", 8) == 3 && ws[2] == 0xe9 && wcslen(ws) == 3);
    char narrow[8]; assert(wcstombs(narrow, ws, sizeof(narrow)) == 4 && !strcmp(narrow, "hi\xc3\xa9"));
    assert(mbstowcs(ws, "\xff", 8) == (size_t)-1);
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    assert(pthread_mutex_lock(&mutex) == 0 && pthread_mutex_trylock(&mutex) == RIX_EBUSY);
    assert(pthread_mutex_unlock(&mutex) == 0 && pthread_mutex_trylock(&mutex) == 0);
    assert(pthread_mutex_unlock(&mutex) == 0 && pthread_mutex_destroy(&mutex) == 0);
    assert(pthread_mutex_lock(0) == RIX_EINVAL);
    pthread_once_t once = PTHREAD_ONCE_INIT;
    assert(pthread_once(&once, once_function) == 0 && pthread_once(&once, once_function) == 0 && once_counter == 1);
    assert(pthread_create(0, 0, 0, 0) == RIX_ENOSYS && pthread_join(0, 0) == RIX_ENOSYS);
    assert(pthread_self() == 42 && pthread_equal(1, 1) && !pthread_equal(1, 2));
    opterr = 0; optind = 1;
    char *go_argv[] = { "prog", "-a", "-b", "val", "rest" };
    assert(getopt(5, go_argv, "ab:") == 'a');
    assert(getopt(5, go_argv, "ab:") == 'b' && optarg && strcmp(optarg, "val") == 0);
    assert(getopt(5, go_argv, "ab:") == -1 && strcmp(go_argv[optind], "rest") == 0);
    optind = 1; char *go_bad[] = { "prog", "-z" };
    assert(getopt(2, go_bad, "ab:") == '?');
    assert(sysconf(_SC_PAGESIZE) == 4096 && sysconf(_SC_CLK_TCK) == 100 && getpagesize() == 4096);
    assert(sysconf(_SC_OPEN_MAX) == 64 && sysconf(9999) == -1);
    uint32_t parsed = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &parsed) == 1 && ntohl(parsed) == INADDR_LOOPBACK);
    assert(inet_pton(AF_INET, "300.1.1.1", &parsed) == 0 && inet_pton(AF_INET, "abc", &parsed) == 0);
    assert(inet_addr("10.0.2.15") == htonl(0x0a00020fu));
    char ip_text[16]; struct in_addr loop = { htonl(INADDR_LOOPBACK) };
    assert(inet_ntop(AF_INET, &loop.s_addr, ip_text, sizeof(ip_text)) && strcmp(ip_text, "127.0.0.1") == 0);
    assert(inet_ntoa(loop) && strcmp(inet_ntoa(loop), "127.0.0.1") == 0);
    assert(htons(0x1234u) == 0x3412u && htonl(0x12345678u) == 0x78563412u);
    struct timeval moment = { 0, 0 };
    assert(gettimeofday(&moment, 0) == 0 && moment.tv_sec == 1700000000 && moment.tv_usec == 0);
    assert(strcmp(strerror(RIX_ESRCH), "No such process") == 0);
    assert(strcmp(strerror(RIX_ETIMEDOUT), "Connection timed out") == 0);
    assert(strcmp(strerror(RIX_EMSGSIZE), "Message too long") == 0);
    assert(stdin && stdout && stderr && fileno(stdin) == 0 && fileno(stdout) == 1);
    assert(atexit(noop_handler) == 0 && atexit(0) == -1);
    errno = 0; assert(system("true") == -1 && errno == RIX_ENOSYS);
    return 0;
}
