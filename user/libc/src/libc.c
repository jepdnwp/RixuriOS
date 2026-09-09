#include "string.h"
#include "stdlib.h"
#include "errno.h"
#include "unistd.h"
#include "stdio.h"
#include "dirent.h"
#include "fcntl.h"
#include <stdint.h>
#include <stdarg.h>

int errno;

void *memcpy(void *destination, const void *source, size_t length) {
    if (!destination || !source) return destination;
    uint8_t *out = (uint8_t *)destination; const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
    return destination;
}
void *memmove(void *destination, const void *source, size_t length) {
    if (!destination || !source || destination == source) return destination;
    uint8_t *out = (uint8_t *)destination; const uint8_t *in = (const uint8_t *)source;
    if (out < in) for (size_t i = 0; i < length; ++i) out[i] = in[i];
    else for (size_t i = length; i; --i) out[i - 1] = in[i - 1];
    return destination;
}
void *memset(void *destination, int value, size_t length) {
    if (!destination) return destination;
    uint8_t *out = (uint8_t *)destination;
    for (size_t i = 0; i < length; ++i) out[i] = (uint8_t)value;
    return destination;
}
int memcmp(const void *left, const void *right, size_t length) {
    const uint8_t *a = (const uint8_t *)left, *b = (const uint8_t *)right;
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    for (size_t i = 0; i < length; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
size_t strlen(const char *text) { size_t n = 0; while (text && text[n]) ++n; return n; }
int strcmp(const char *left, const char *right) {
    size_t i = 0; if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (left[i] && left[i] == right[i]) ++i;
    return (unsigned char)left[i] == (unsigned char)right[i] ? 0 :
           ((unsigned char)left[i] < (unsigned char)right[i] ? -1 : 1);
}
int strncmp(const char *left, const char *right, size_t length) {
    if (!length) return 0;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    for (size_t i = 0; i < length; ++i) {
        if (left[i] != right[i]) return (unsigned char)left[i] < (unsigned char)right[i] ? -1 : 1;
        if (!left[i]) return 0;
    }
    return 0;
}
char *strchr(const char *text, int value) {
    if (!text) return 0;
    for (;;) { if ((unsigned char)*text == (unsigned char)value) return (char *)text; if (!*text) return 0; ++text; }
}
char *strerror(int error) {
    switch (error) {
    case RIX_EPERM: return "Operation not permitted"; case RIX_ENOENT: return "No such file or directory";
    case RIX_EIO: return "I/O error"; case RIX_EBADF: return "Bad file descriptor";
    case RIX_ENOMEM: return "Out of memory"; case RIX_EACCES: return "Permission denied";
    case RIX_EFAULT: return "Bad address"; case RIX_EEXIST: return "File exists";
    case RIX_EINVAL: return "Invalid argument"; case RIX_ENOSPC: return "No space left";
    case RIX_ENOSYS: return "Function not implemented"; case RIX_EPIPE: return "Broken pipe";
    case RIX_EINTR: return "Interrupted system call"; case RIX_ERANGE: return "Result out of range";
    default: return "Unknown error";
    }
}
int strerror_r(int error, char *buffer, size_t capacity) {
    if (!buffer || !capacity) { errno = RIX_EINVAL; return RIX_EINVAL; }
    const char *message = strerror(error); size_t length = strlen(message);
    size_t copy = length < capacity - 1u ? length : capacity - 1u;
    memcpy(buffer, message, copy); buffer[copy] = 0;
    return length < capacity ? 0 : RIX_ERANGE;
}

typedef struct { uint32_t magic; uint32_t used; size_t size; } rix_alloc_header_t;
#define RIX_ALLOC_MAGIC 0x52495841u
static size_t align_up(size_t value) { return (value + 15u) & ~(size_t)15u; }
void *malloc(size_t size) {
    if (!size || size > (size_t)-1 - sizeof(rix_alloc_header_t)) { errno = RIX_ENOMEM; return 0; }
    size_t total = align_up(sizeof(rix_alloc_header_t) + size);
    void *memory = sbrk((ptrdiff_t)total);
    if (memory == (void *)-1) { errno = RIX_ENOMEM; return 0; }
    rix_alloc_header_t *header = (rix_alloc_header_t *)memory;
    header->magic = RIX_ALLOC_MAGIC; header->used = 1; header->size = size;
    return header + 1;
}
void *calloc(size_t count, size_t size) {
    if (count && size > (size_t)-1 / count) { errno = RIX_ENOMEM; return 0; }
    void *pointer = malloc(count * size); if (pointer) memset(pointer, 0, count * size); return pointer;
}
void free(void *pointer) {
    if (!pointer) return;
    rix_alloc_header_t *header = ((rix_alloc_header_t *)pointer) - 1;
    if (header->magic == RIX_ALLOC_MAGIC) header->used = 0;
}
void *realloc(void *pointer, size_t size) {
    if (!pointer) return malloc(size);
    if (!size) { free(pointer); return 0; }
    rix_alloc_header_t *header = ((rix_alloc_header_t *)pointer) - 1;
    if (header->magic == RIX_ALLOC_MAGIC && header->used && header->size >= size) { header->size = size; return pointer; }
    void *replacement = malloc(size);
    if (!replacement) return 0;
    if (header->magic == RIX_ALLOC_MAGIC && header->used) memcpy(replacement, pointer, header->size < size ? header->size : size);
    free(pointer); return replacement;
}


int isdigit(int value) { return value >= '0' && value <= '9'; }
int islower(int value) { return value >= 'a' && value <= 'z'; }
int isupper(int value) { return value >= 'A' && value <= 'Z'; }
int isalpha(int value) { return islower(value) || isupper(value); }
int isalnum(int value) { return isalpha(value) || isdigit(value); }
int isspace(int value) { return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' || value == '\f'; }
int tolower(int value) { return isupper(value) ? value + ('a' - 'A') : value; }
int toupper(int value) { return islower(value) ? value - ('a' - 'A') : value; }

static void format_char(char *buffer, size_t capacity, size_t *written, char value) {
    if (capacity && *written + 1u < capacity) buffer[*written] = value;
    ++*written;
}
static void format_text(char *buffer, size_t capacity, size_t *written, const char *text) {
    if (!text) text = "(null)";
    while (*text) format_char(buffer, capacity, written, *text++);
}
static void format_unsigned(char *buffer, size_t capacity, size_t *written, uint64_t value, unsigned base) {
    char digits[sizeof(uint64_t) * 2u + 1u]; size_t count = 0;
    do { unsigned digit = (unsigned)(value % base); digits[count++] = (char)(digit < 10u ? '0' + digit : 'a' + digit - 10u); value /= base; } while (value);
    while (count) format_char(buffer, capacity, written, digits[--count]);
}
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments) {
    size_t written = 0;
    if (!format || (!buffer && capacity)) return -1;
    while (*format) {
        if (*format != '%') { format_char(buffer, capacity, &written, *format++); continue; }
        ++format; if (!*format) break;
        if (*format == '%') { format_char(buffer, capacity, &written, '%'); ++format; continue; }
        int long_value = 0; if (*format == 'l') { long_value = 1; ++format; }
        if (*format == 'z') { long_value = 1; ++format; }
        switch (*format++) {
        case 'c': format_char(buffer, capacity, &written, (char)va_arg(arguments, int)); break;
        case 's': format_text(buffer, capacity, &written, va_arg(arguments, const char *)); break;
        case 'd': {
            int64_t value = long_value ? va_arg(arguments, long) : va_arg(arguments, int);
            if (value < 0) { format_char(buffer, capacity, &written, '-'); format_unsigned(buffer, capacity, &written, (uint64_t)(-(value + 1)) + 1u, 10); }
            else format_unsigned(buffer, capacity, &written, (uint64_t)value, 10);
            break;
        }
        case 'u': format_unsigned(buffer, capacity, &written, long_value ? va_arg(arguments, unsigned long) : va_arg(arguments, unsigned), 10); break;
        case 'x': format_unsigned(buffer, capacity, &written, long_value ? va_arg(arguments, unsigned long) : va_arg(arguments, unsigned), 16); break;
        case 'p': format_text(buffer, capacity, &written, "0x"); format_unsigned(buffer, capacity, &written, (uint64_t)(uintptr_t)va_arg(arguments, void *), 16); break;
        default: format_char(buffer, capacity, &written, '?'); break;
        }
    }
    if (capacity) buffer[written < capacity ? written : capacity - 1u] = 0;
    return (int)written;
}
int snprintf(char *buffer, size_t capacity, const char *format, ...) {
    va_list arguments; va_start(arguments, format); int result = vsnprintf(buffer, capacity, format, arguments); va_end(arguments); return result;
}
static FILE standard_output = { .fd = 1 };
static FILE standard_error = { .fd = 2 };
static FILE *libc_stdout = &standard_output;
static FILE *libc_stderr = &standard_error;
int vfprintf(FILE *stream, const char *format, va_list arguments) {
    if (!stream || !format) { errno = RIX_EINVAL; return -1; }
    va_list sizing; va_copy(sizing, arguments);
    int length = vsnprintf(0, 0, format, sizing); va_end(sizing);
    if (length < 0) return -1;
    char *text = malloc((size_t)length + 1u);
    if (!text) { errno = RIX_ENOMEM; return -1; }
    va_list rendering; va_copy(rendering, arguments);
    int rendered = vsnprintf(text, (size_t)length + 1u, format, rendering); va_end(rendering);
    if (rendered < 0 || fwrite(text, 1, (size_t)rendered, stream) != (size_t)rendered) rendered = -1;
    free(text); return rendered;
}
int fprintf(FILE *stream, const char *format, ...) { va_list arguments; va_start(arguments, format); int result = vfprintf(stream, format, arguments); va_end(arguments); return result; }
int vprintf(const char *format, va_list arguments) { return vfprintf(libc_stdout, format, arguments); }
int printf(const char *format, ...) { va_list arguments; va_start(arguments, format); int result = vprintf(format, arguments); va_end(arguments); return result; }
void perror(const char *prefix) { if (!prefix) prefix = "error"; (void)fprintf(libc_stderr, "%s: errno %d\n", prefix, errno); }
int puts(const char *text) {
    size_t length = strlen(text); if (write(1, text, length) < 0 || write(1, "\n", 1) < 0) return -1; return (int)(length + 1u);
}
int putchar(int value) { char character = (char)value; return write(1, &character, 1) < 0 ? -1 : (unsigned char)character; }

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode || !mode[0]) { errno = RIX_EINVAL; return 0; }
    uint32_t flags = mode[0] == 'r' ? O_RDONLY : (mode[0] == 'a' ? O_WRONLY | O_CREAT | O_APPEND : O_WRONLY | O_CREAT | O_TRUNC);
    if (mode[1] == '+') flags = (flags & ~(O_RDONLY | O_WRONLY)) | O_RDWR;
    int fd = open(path, flags, 0666u);
    if (fd < 0) return 0;
    FILE *stream = malloc(sizeof(*stream));
    if (!stream) { (void)close(fd); return 0; }
    stream->fd = fd; stream->mode = _IOFBF;
    stream->buffer = malloc(BUFSIZ);
    if (stream->buffer) { stream->buffer_size = BUFSIZ; stream->owns_buffer = 1; }
    return stream;
}
static int stream_flush(FILE *stream) {
    if (!stream || !stream->writing || !stream->buffer || !stream->buffer_pos) return 0;
    size_t done=0; while (done<stream->buffer_pos) { rix_ssize_t n=write(stream->fd,stream->buffer+done,stream->buffer_pos-done); if (n<=0) { stream->error=1; return -1; } done+=(size_t)n; }
    stream->buffer_pos=0; return 0;
}
static int stream_discard_input(FILE *stream) {
    if (!stream || stream->writing || !stream->buffer || stream->buffer_len<=stream->buffer_pos) return 0;
    if (lseek(stream->fd,-(off_t)(stream->buffer_len-stream->buffer_pos),SEEK_CUR)<0) return -1;
    stream->buffer_pos=0; stream->buffer_len=0; return 0;
}
static int stream_fill(FILE *stream) {
    if (!stream || !stream->buffer || !stream->buffer_size) return -1;
    rix_ssize_t n=read(stream->fd,stream->buffer,stream->buffer_size); if (n==0) { stream->eof=1; return -1; } if (n<0) { stream->error=1; return -1; }
    stream->buffer_pos=0; stream->buffer_len=(size_t)n; stream->writing=0; return 0;
}
int fclose(FILE *stream) { if (!stream) { errno = RIX_EINVAL; return -1; } int rc=stream_flush(stream); if (stream->owns_buffer) free(stream->buffer); int close_rc=close(stream->fd); free(stream); return rc<0?rc:close_rc; }
size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream || (!buffer && size && count)) { errno = RIX_EINVAL; return 0; }
    if (!size || count > (size_t)-1 / size) return 0;
    if (stream_flush(stream)<0) return 0;
    stream->writing=0; size_t total=size*count,done=0; unsigned char*out=buffer;
    while(done<total){int value=fgetc(stream);if(value==EOF)break;out[done++]=(unsigned char)value;} return done/size;
}
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream || (!buffer && size && count)) { errno = RIX_EINVAL; return 0; }
    if (!size || count > (size_t)-1 / size) return 0;
    size_t total=size*count,done=0;const unsigned char*in=buffer;while(done<total){if(fputc(in[done],stream)==EOF)break;++done;}return done/size;
}
int fflush(FILE *stream) { if (!stream) { errno = RIX_EINVAL; return -1; } return stream_flush(stream); }
int fseek(FILE *stream, long offset, int whence) { if (!stream) { errno = RIX_EINVAL; return -1; } if(stream_flush(stream)<0||stream_discard_input(stream)<0)return -1; stream->writing=0;stream->buffer_pos=stream->buffer_len=0;return lseek(stream->fd,(off_t)offset,whence)<0?-1:0; }
long ftell(FILE *stream) { if (!stream) { errno = RIX_EINVAL; return -1L; } long base=(long)lseek(stream->fd,0,SEEK_CUR);if(base<0)return -1;if(!stream->writing&&stream->buffer_len>stream->buffer_pos)base-=(long)(stream->buffer_len-stream->buffer_pos);return base; }
void rewind(FILE *stream) { if (stream) (void)fseek(stream, 0, SEEK_SET); }
int fgetc(FILE *stream) { if(!stream){errno=RIX_EINVAL;return EOF;}if(stream->writing&&stream_flush(stream)<0)return EOF;stream->writing=0;if(stream->buffer){if(stream->buffer_pos>=stream->buffer_len&&stream_fill(stream)<0)return EOF;return stream->buffer[stream->buffer_pos++];}unsigned char value;rix_ssize_t n=read(stream->fd,&value,1);if(n==0)stream->eof=1;if(n<0)stream->error=1;return n==1?(int)value:EOF; }
int fputc(int value, FILE *stream) { if(!stream){errno=RIX_EINVAL;return EOF;}if(!stream->writing){if(stream_discard_input(stream)<0)return EOF;stream->writing=1;}unsigned char character=(unsigned char)value;if(!stream->buffer){if(write(stream->fd,&character,1)!=1){stream->error=1;return EOF;}return(int)character;}if(stream->buffer_pos>=stream->buffer_size&&stream_flush(stream)<0)return EOF;stream->buffer[stream->buffer_pos++]=character;return(int)character; }
char *fgets(char *buffer, int capacity, FILE *stream) {
    if (!buffer || capacity <= 0 || !stream) { errno=RIX_EINVAL; return 0; }
    int index=0; while (index+1 < capacity) { int value=fgetc(stream); if (value==EOF) break; buffer[index++]=(char)value; if (value=='\n') break; }
    if (!index) return 0;
    buffer[index]=0; return buffer;
}
int fputs(const char *text, FILE *stream) { if (!text || !stream) { errno=RIX_EINVAL; return EOF; } size_t length=strlen(text);return fwrite(text,1,length,stream)==length?0:EOF; }
int setvbuf(FILE *stream, char *buffer, int mode, size_t size) { if (!stream || mode < _IONBF || mode > _IOLBF || (size && !buffer)) { errno=RIX_EINVAL; return -1; } if(stream_flush(stream)<0)return -1;if(stream->owns_buffer)free(stream->buffer);stream->buffer=mode==_IONBF?0:(unsigned char*)buffer;stream->buffer_size=mode==_IONBF?0:size;stream->buffer_pos=stream->buffer_len=0;stream->mode=mode;stream->owns_buffer=0;stream->writing=0;return 0; }
void setbuf(FILE *stream, char *buffer) { (void)setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, buffer ? BUFSIZ : 0); }
int feof(FILE *stream) { return stream ? stream->eof : 0; }
int ferror(FILE *stream) { return stream ? stream->error : 0; }
void clearerr(FILE *stream) { if (stream) { stream->eof = 0; stream->error = 0; } }

DIR *opendir(const char *path) {
    if (!path) { errno = RIX_EINVAL; return 0; }
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    DIR *directory = malloc(sizeof(*directory));
    if (!directory) { (void)close(fd); return 0; }
    directory->fd = fd; directory->index = 0; directory->count = 0;
    if (getdents(fd, (rix_dirent_t *)directory->entries, 16u, &directory->count) < 0) { (void)close(fd); free(directory); return 0; }
    return directory;
}
struct dirent *readdir(DIR *directory) {
    if (!directory) { errno = RIX_EINVAL; return 0; }
    if (directory->index >= directory->count) {
        directory->index = 0; directory->count = 0;
        size_t fetched = 0;
        if (getdents(directory->fd, (rix_dirent_t *)directory->entries, 16u, &fetched) < 0 || !fetched) return 0;
        directory->count = fetched;
    }
    return &directory->entries[directory->index++];
}
int closedir(DIR *directory) { if (!directory) { errno = RIX_EINVAL; return -1; } int rc = close(directory->fd); free(directory); return rc; }


static int conversion_digit(int value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'z') return value - 'a' + 10;
    if (value >= 'A' && value <= 'Z') return value - 'A' + 10;
    return -1;
}
static unsigned long parse_unsigned(const char *text, char **end, int base, int *negative, int *range) {
    const char *p=text; while (isspace((unsigned char)*p)) ++p;
    *negative=0; if (*p=='+' || *p=='-') { *negative=*p=='-'; ++p; }
    if (base==0) { base=10; if (*p=='0') { base=8; if (p[1]=='x'||p[1]=='X') { base=16; p+=2; } } }
    else if (base==16 && p[0]=='0' && (p[1]=='x'||p[1]=='X')) p+=2;
    if (base<2 || base>36) { errno=RIX_EINVAL; if(end)*end=(char*)text; return 0; }
    const char *start=p; unsigned long value=0,limit=~0UL; *range=0;
    for (;;) { int digit=conversion_digit((unsigned char)*p); if(digit<0||digit>=base)break; if(value>(limit-(unsigned long)digit)/(unsigned)base){*range=1;value=limit;}else if(!*range)value=value*(unsigned)base+(unsigned)digit; ++p; }
    if (end) *end=(char*)(p==start?text:p);
    return value;
}
unsigned long strtoul(const char *text, char **end, int base) { if(!text){errno=RIX_EINVAL;if(end)*end=0;return 0;}int negative=0,range=0;unsigned long value=parse_unsigned(text,end,base,&negative,&range);if(range)errno=RIX_ERANGE;return negative?0UL-value:value; }
long strtol(const char *text, char **end, int base) { if(!text){errno=RIX_EINVAL;if(end)*end=0;return 0;}int negative=0,range=0;unsigned long value=parse_unsigned(text,end,base,&negative,&range);unsigned long max=(~0UL>>1),limit=negative?max+1UL:max;if(value>limit){value=limit;range=1;}if(range)errno=RIX_ERANGE;if(negative)return value==max+1UL?(long)(-(long)max-1L):-(long)value;return(long)value; }
int atoi(const char *text) { return (int)strtol(text,0,10); }
int abs(int value) { return value<0 ? -value : value; }
long labs(long value) { return value<0 ? -value : value; }
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    if (!base || !size || !compare || count<2) return;
    unsigned char *item=malloc(size); if(!item){errno=RIX_ENOMEM;return;}
    unsigned char *bytes=base;
    for(size_t i=1;i<count;++i){memcpy(item,bytes+i*size,size);size_t j=i;while(j>0&&compare(bytes+(j-1)*size,item)>0){memcpy(bytes+j*size,bytes+(j-1)*size,size);--j;}memcpy(bytes+j*size,item,size);}
    free(item);
}


#define RIX_ENV_MAX 32u
typedef struct { char *name; char *value; } rix_environment_entry_t;
static rix_environment_entry_t environment[RIX_ENV_MAX];
#ifndef RIX_HOST_TEST
static char *environment_strings[RIX_ENV_MAX + 1u];
char **environ = environment_strings;
#endif
static uint32_t random_state = 1u;
static int environment_name_valid(const char *name) { if(!name||!*name)return 0;while(*name){if(*name=='=')return 0;++name;}return 1; }
static int environment_index(const char *name) { if(!environment_name_valid(name))return -1;for(size_t i=0;i<RIX_ENV_MAX;++i)if(environment[i].name&&strcmp(environment[i].name,name)==0)return(int)i;return -1; }
#ifndef RIX_HOST_TEST
static void environment_rebuild(void) { for(size_t i=0;i<RIX_ENV_MAX;++i){if(environment_strings[i])free(environment_strings[i]);environment_strings[i]=0;}size_t out=0;for(size_t i=0;i<RIX_ENV_MAX;++i)if(environment[i].name){size_t n=strlen(environment[i].name),v=strlen(environment[i].value);char *entry=malloc(n+v+2u);if(!entry)continue;memcpy(entry,environment[i].name,n);entry[n]='=';memcpy(entry+n+1u,environment[i].value,v+1u);environment_strings[out++]=entry;}environment_strings[out]=0;}
#endif
char *getenv(const char *name) { int index=environment_index(name);return index<0?0:environment[(size_t)index].value; }
int setenv(const char *name,const char *value,int overwrite) { if(!environment_name_valid(name)||!value){errno=RIX_EINVAL;return -1;}int index=environment_index(name);if(index>=0&&!overwrite)return 0;char *new_name=0,*new_value=0;if(index<0){for(size_t i=0;i<RIX_ENV_MAX;++i)if(!environment[i].name){index=(int)i;break;}if(index<0){errno=RIX_ENOMEM;return -1;}size_t name_len=strlen(name);new_name=malloc(name_len+1u);if(!new_name){errno=RIX_ENOMEM;return -1;}memcpy(new_name,name,name_len+1u);}size_t value_len=strlen(value);new_value=malloc(value_len+1u);if(!new_value){free(new_name);errno=RIX_ENOMEM;return -1;}memcpy(new_value,value,value_len+1u);if(environment[(size_t)index].value)free(environment[(size_t)index].value);if(new_name)environment[(size_t)index].name=new_name;environment[(size_t)index].value=new_value;
#ifndef RIX_HOST_TEST
environment_rebuild();
#endif
return 0; }
int putenv(char *string) { if(!string){errno=RIX_EINVAL;return -1;}char *equals=strchr(string,'=');if(!equals||equals==string){errno=RIX_EINVAL;return -1;}size_t length=(size_t)(equals-string);char *name=malloc(length+1u);if(!name){errno=RIX_ENOMEM;return -1;}memcpy(name,string,length);name[length]=0;int result=setenv(name,equals+1,1);free(name);return result; }
int unsetenv(const char *name) { if(!environment_name_valid(name)){errno=RIX_EINVAL;return -1;}int index=environment_index(name);if(index<0)return 0;free(environment[(size_t)index].name);free(environment[(size_t)index].value);environment[(size_t)index].name=0;environment[(size_t)index].value=0;
#ifndef RIX_HOST_TEST
environment_rebuild();
#endif
return 0; }
int clearenv(void) { for(size_t i=0;i<RIX_ENV_MAX;++i)if(environment[i].name){free(environment[i].name);free(environment[i].value);environment[i].name=0;environment[i].value=0;}
#ifndef RIX_HOST_TEST
environment_rebuild();
#endif
return 0; }
void srand(unsigned seed) { random_state=seed?seed:1u; }
int rand(void) { random_state=random_state*1103515245u+12345u;return(int)((random_state>>1)&0x7fffffffU); }
uint32_t arc4random(void) { uint32_t value=0; if (getrandom(&value,sizeof(value),0)==(rix_ssize_t)sizeof(value)) return value; random_state=random_state*1664525u+1013904223u;return random_state; }
