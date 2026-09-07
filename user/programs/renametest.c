#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
#define RIX_EINVAL 22
#define RIX_EEXIST 17

static size_t text_length(const char *text) { size_t length=0; while(text&&text[length])++length; return length; }
static int emit(const char *text) { size_t length=text_length(text); return write(1,text,length)==(rix_ssize_t)length?0:-1; }
static int same_stat(const rix_stat_t *left,const rix_stat_t *right) { return left&&right&&left->inode==right->inode&&left->type==right->type&&left->mode==right->mode&&left->uid==right->uid&&left->gid==right->gid&&left->size==right->size; }
static int create_file(const char *path,uint32_t mode) { int fd=openat(RIX_VFS_AT_FDCWD,path,RIX_VFS_O_WRONLY|RIX_VFS_O_CREAT|RIX_VFS_O_TRUNC,mode); if(fd<0)return-1; char value='r'; int status=write(fd,&value,1)==1?0:-1; if(close(fd)!=0)status=-1; return status; }
int program_main(int argc,char **argv,char **envp) {
    rix_stat_t before,after; (void)argc;(void)argv;(void)envp;
    (void)unlink("/usr/rename-source");(void)unlink("/usr/rename-target");(void)unlink("/usr/rename-existing");(void)unlink("/etc/rename-cross-target");(void)unlink("/etc/rename-cross-source");(void)unlink("/usr/rename-cross-existing");(void)rmdir("/usr/rename-dir");
    if(create_file("/usr/rename-source",0670u)!=0||stat("/usr/rename-source",&before)!=0||rename("/usr/rename-source","/usr/rename-target")!=0||stat("/usr/rename-source",&after)!=-RIX_EINVAL||stat("/usr/rename-target",&after)!=0||!same_stat(&before,&after))return 1;
    if(emit("rename-inode=PASS\n")!=0)return 1;
    if(create_file("/usr/rename-existing",0600u)!=0||rename("/usr/rename-target","/usr/rename-existing")!=0||stat("/usr/rename-target",&after)!=-RIX_EINVAL||stat("/usr/rename-existing",&after)!=0||!same_stat(&before,&after))return 1;
    if(emit("rename-overwrite=PASS\n")!=0)return 1;
    if(mkdir("/usr/rename-dir",0755u)!=0||rename("/usr/rename-existing","/usr/rename-dir")!=-RIX_EEXIST||stat("/usr/rename-existing",&after)!=0||stat("/usr/rename-dir",&after)!=0||rmdir("/usr/rename-dir")!=0)return 1;
    if(emit("rename-failure-safe=PASS\n")!=0)return 1;
    if(rename("/usr/rename-existing","/etc/rename-cross-target")!=0||stat("/usr/rename-existing",&after)!=-RIX_EINVAL||stat("/etc/rename-cross-target",&after)!=0||!same_stat(&before,&after))return 1;
    if(emit("rename-crossdir=PASS\n")!=0)return 1;
    if(rename("/etc/rename-cross-target","/usr/rename-source")!=0||stat("/usr/rename-source",&after)!=0||!same_stat(&before,&after)||unlink("/usr/rename-source")!=0)return 1;
    if(create_file("/etc/rename-cross-source",0660u)!=0||stat("/etc/rename-cross-source",&before)!=0||create_file("/usr/rename-cross-existing",0600u)!=0||rename("/etc/rename-cross-source","/usr/rename-cross-existing")!=0||stat("/etc/rename-cross-source",&after)!=-RIX_EINVAL||stat("/usr/rename-cross-existing",&after)!=0||!same_stat(&before,&after)||unlink("/usr/rename-cross-existing")!=0)return 1;
    if(emit("rename-cross-overwrite=PASS\n")!=0)return 1;
    return emit("rename-roundtrip=PASS\n")==0?0:1;
}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
