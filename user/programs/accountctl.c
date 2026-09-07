#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include "auth_crypto.h"

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
#define RIX_CAP_DAC_OVERRIDE (1ULL << 0)
#define RIX_CAP_SETUID (1ULL << 1)
#define RIX_EACCES 13
#define RIX_EINVAL 22
#define RIX_MAX_FILE 4096u
#define RIX_SALT_LENGTH 32u
#define RIX_HASH_HEX 64u

static size_t length(const char *text) { size_t n=0; while(text&&text[n])++n; return n; }
static int equal(const char *a,const char *b) { size_t i=0; if(!a||!b)return 0; while(a[i]&&b[i]&&a[i]==b[i])++i; return a[i]==0&&b[i]==0; }
static int emit(const char *text) { size_t n=length(text); return write(1,text,n)==(rix_ssize_t)n?0:-1; }
static int valid_name(const char *name) { size_t n=length(name); if(n==0||n>=64)return 0; for(size_t i=0;i<n;++i)if(name[i]==':'||name[i]=='\n'||name[i]=='/')return 0; return 1; }
static int append_text(char *out,size_t cap,size_t *used,const char *text) { size_t n=length(text); if(!out||!used||*used+n>=cap)return -1; for(size_t i=0;i<n;++i)out[(*used)++]=text[i]; out[*used]=0; return 0; }
static int append_uint(char *out,size_t cap,size_t *used,uint32_t value) { char digits[11];size_t n=0; if(value==0)digits[n++]='0'; while(value){digits[n++]=(char)('0'+value%10u);value/=10u;} if(*used+n>=cap)return -1; while(n)out[(*used)++]=digits[--n];out[*used]=0;return 0; }
static int read_file(const char *path,char *buffer,size_t cap) { int fd=openat(RIX_VFS_AT_FDCWD,path,0,0);if(fd<0)return-1;size_t used=0;for(;;){rix_ssize_t got=read(fd,buffer+used,cap-used-1u);if(got<0||used+(size_t)(got<0?0:got)>=cap-1u){close(fd);return-1;}if(got==0)break;used+=(size_t)got;}buffer[used]=0;return close(fd); }
static int line_name(const char *line,const char *name) { size_t i=0; while(line[i]&&line[i]!=':'&&line[i]!='\n'&&name[i]&&line[i]==name[i])++i; return line[i]==':'&&name[i]==0; }
static const char *next_line(const char *cursor,char *line,size_t cap) { size_t n=0; if(!cursor||!*cursor)return cursor; while(cursor[n]&&cursor[n]!='\n'){if(n+1>=cap)return 0;line[n]=cursor[n];++n;} line[n]=0; if(cursor[n]=='\n')++n; return cursor+n; }
static int build_passwd(const char *input,const char *name,uint32_t uid,uint32_t gid,int mode,char *output,size_t cap) {
    const char *cursor=input;char line[512];size_t used=0;int found=0;output[0]=0;
    while(cursor&&*cursor){const char *next=next_line(cursor,line,sizeof(line));if(!next)return-1;if(!line_name(line,name)){if(append_text(output,cap,&used,line)||append_text(output,cap,&used,"\n"))return-1;}else{found=1;if(mode==1){if(append_text(output,cap,&used,name)||append_text(output,cap,&used,":" )||append_uint(output,cap,&used,uid)||append_text(output,cap,&used,":" )||append_uint(output,cap,&used,gid)||append_text(output,cap,&used,":/home/operator:/bin/sh\n"))return-1;}else if(mode!=2){if(append_text(output,cap,&used,line)||append_text(output,cap,&used,"\n"))return-1;}}cursor=next;}
    if(mode==0&&!found){if(append_text(output,cap,&used,name)||append_text(output,cap,&used,":" )||append_uint(output,cap,&used,uid)||append_text(output,cap,&used,":" )||append_uint(output,cap,&used,gid)||append_text(output,cap,&used,":/home/operator:/bin/sh\n"))return-1;}
    return (mode==2&&!found)||(mode==0&&found)||(mode==3&&!found)?-1:0;
}
static void salt_for(const char *name,char salt[RIX_SALT_LENGTH+1]) { uint32_t state=0x9e3779b9u;for(size_t i=0;name[i];++i)state=state*33u+(uint8_t)name[i];for(size_t i=0;i<RIX_SALT_LENGTH;++i){state=state*1664525u+1013904223u;salt[i]="0123456789abcdef"[(state>>28)&15u];}salt[RIX_SALT_LENGTH]=0; }
static int hex_digest(const uint8_t digest[32],char output[RIX_HASH_HEX+1]) { static const char hex[]="0123456789abcdef";for(size_t i=0;i<32;++i){output[i*2]=hex[digest[i]>>4];output[i*2+1]=hex[digest[i]&15u];}output[RIX_HASH_HEX]=0;return 0; }
static int build_shadow(const char *input,const char *name,const char *password,int mode,char *output,size_t cap) {
    const char *cursor=input;char line[512],salt[RIX_SALT_LENGTH+1],hash[RIX_HASH_HEX+1];size_t used=0;int found=0;output[0]=0;
    if(mode!=2){salt_for(name,salt);uint8_t digest[32];rix_password_digest(salt,password,128u,digest);hex_digest(digest,hash);}
    while(cursor&&*cursor){const char *next=next_line(cursor,line,sizeof(line));if(!next)return-1;if(!line_name(line,name)){if(append_text(output,cap,&used,line)||append_text(output,cap,&used,"\n"))return-1;}else{found=1;if(mode==1){if(append_text(output,cap,&used,name)||append_text(output,cap,&used,":rixsha256:")||append_text(output,cap,&used,salt)||append_text(output,cap,&used,":128:")||append_text(output,cap,&used,hash)||append_text(output,cap,&used,"\n"))return-1;}}cursor=next;}
    if(mode==0&&!found){if(append_text(output,cap,&used,name)||append_text(output,cap,&used,":rixsha256:")||append_text(output,cap,&used,salt)||append_text(output,cap,&used,":128:")||append_text(output,cap,&used,hash)||append_text(output,cap,&used,"\n"))return-1;}
    return (mode==2&&!found)||(mode==0&&found)||(mode==1&&!found)?-1:0;
}
static int build_shadow_lock(const char *input,const char *name,int lock,char *output,size_t cap) {
    const char *cursor=input;char line[512];size_t used=0,name_length=length(name);int found=0;output[0]=0;
    while(cursor&&*cursor){const char *next=next_line(cursor,line,sizeof(line));if(!next)return-1;if(!line_name(line,name)){if(append_text(output,cap,&used,line)||append_text(output,cap,&used,"\n"))return-1;}else{const char *rest=line+name_length+1;found=1;if(append_text(output,cap,&used,name)||append_text(output,cap,&used,":"))return-1;if(lock){if(append_text(output,cap,&used,"!")||append_text(output,cap,&used,rest))return-1;}else{if(rest[0]=='!')++rest;if(append_text(output,cap,&used,rest))return-1;}if(append_text(output,cap,&used,"\n"))return-1;}cursor=next;}
    return found?0:-1;
}
static int write_file(const char *path,const char *data,uint32_t mode) { int fd=openat(RIX_VFS_AT_FDCWD,path,RIX_VFS_O_WRONLY|RIX_VFS_O_CREAT|RIX_VFS_O_TRUNC,mode);if(fd<0)return-1;size_t n=length(data);int rc=write(fd,data,n)==(rix_ssize_t)n?0:-1;if(close(fd)!=0)rc=-1;return rc; }
static int commit_stores(const char *passwd,const char *shadow) {
    int passwd_moved=0,shadow_moved=0,passwd_new=0,shadow_new=0;
    (void)unlink("/etc/passwd.bak");(void)unlink("/etc/shadow.bak");
    if(write_file("/etc/passwd.new",passwd,0644u)!=0||write_file("/etc/shadow.new",shadow,0600u)!=0)goto rollback;
    passwd_new=shadow_new=1;
    if(rename("/etc/passwd","/etc/passwd.bak")!=0)goto rollback;
    passwd_moved=1;
    if(rename("/etc/shadow","/etc/shadow.bak")!=0)goto rollback;
    shadow_moved=1;
    if(rename("/etc/passwd.new","/etc/passwd")!=0)goto rollback;
    passwd_new=0;
    if(rename("/etc/shadow.new","/etc/shadow")!=0)goto rollback;
    shadow_new=0;
    (void)unlink("/etc/passwd.bak");(void)unlink("/etc/shadow.bak");return 0;
rollback:
    if(passwd_new)(void)unlink("/etc/passwd.new");
    if(shadow_new)(void)unlink("/etc/shadow.new");
    if(passwd_moved){(void)unlink("/etc/passwd");(void)rename("/etc/passwd.bak","/etc/passwd");}
    if(shadow_moved){(void)unlink("/etc/shadow");(void)rename("/etc/shadow.bak","/etc/shadow");}
    return -1;
}
int program_main(int argc,char **argv,char **envp) {
    char passwd[RIX_MAX_FILE],shadow[RIX_MAX_FILE],new_passwd[RIX_MAX_FILE],new_shadow[RIX_MAX_FILE];(void)envp;
    if(getuid()!=0)return -RIX_EACCES;
    if(argc<3||!valid_name(argv[1]))return -RIX_EINVAL;
    if(read_file("/etc/passwd",passwd,sizeof(passwd))!=0||read_file("/etc/shadow",shadow,sizeof(shadow))!=0)return -1;
    if(equal(argv[1],"add")){uint32_t uid=0,gid=0;if(argc!=5||!valid_name(argv[2]))return-RIX_EINVAL;for(size_t i=0;argv[3][i];++i){if(argv[3][i]<'0'||argv[3][i]>'9')return-RIX_EINVAL;uid=uid*10u+(uint32_t)(argv[3][i]-'0');}gid=uid;if(build_passwd(passwd,argv[2],uid,gid,0,new_passwd,sizeof(new_passwd))!=0)return-1;if(build_shadow(shadow,argv[2],argv[4],0,new_shadow,sizeof(new_shadow))!=0)return-1;if(commit_stores(new_passwd,new_shadow)!=0)return-1;return emit("account-add=PASS\n");}
    if(equal(argv[1],"remove")){if(argc!=3)return-RIX_EINVAL;if(build_passwd(passwd,argv[2],0,0,2,new_passwd,sizeof(new_passwd))!=0||build_shadow(shadow,argv[2],"",2,new_shadow,sizeof(new_shadow))!=0)return-1;if(commit_stores(new_passwd,new_shadow)!=0)return-1;return emit("account-remove=PASS\n");}
    if(equal(argv[1],"rotate")){if(argc!=4)return-RIX_EINVAL;if(build_passwd(passwd,argv[2],0,0,3,new_passwd,sizeof(new_passwd))!=0||build_shadow(shadow,argv[2],argv[3],1,new_shadow,sizeof(new_shadow))!=0)return-1;if(commit_stores(new_passwd,new_shadow)!=0)return-1;return emit("password-rotate=PASS\n");}
    if(equal(argv[1],"lock")||equal(argv[1],"unlock")){int lock=equal(argv[1],"lock");if(argc!=3)return-RIX_EINVAL;if(build_passwd(passwd,argv[2],0,0,3,new_passwd,sizeof(new_passwd))!=0||build_shadow_lock(shadow,argv[2],lock,new_shadow,sizeof(new_shadow))!=0)return-1;if(commit_stores(new_passwd,new_shadow)!=0)return-1;return emit(lock?"account-lock=PASS\n":"account-unlock=PASS\n");}
    return -RIX_EINVAL;
}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
