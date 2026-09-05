#include "vfs.h"
#include <stddef.h>

typedef struct { rix_vnode_t node; } vfs_root_t;
static vfs_root_t root;

static int append_component(char *out,size_t cap,size_t *len,const char *start,size_t n){
    if(n==0)return 0;
    if(*len&&out[*len-1]!='/'){if(*len+1>=cap)return -1;out[(*len)++]='/';}
    if(*len+n>=cap)return -1;
    for(size_t i=0;i<n;i++)out[(*len)++]=start[i];
    return 0;
}

int vfs_normalize_path(const char *input,char *output,size_t output_size){
    if(!input||!output||output_size<2)return -1;
    size_t len=0;output[len++]='/';
    const char *p=input;
    while(*p){
        while(*p=='/')p++;
        if(!*p)break;
        const char *s=p;while(*p&&*p!='/')p++;size_t n=(size_t)(p-s);
        if(n==1&&s[0]=='.')continue;
        if(n==2&&s[0]=='.'&&s[1]=='.'){
            if(len>1){if(output[len-1]=='/')len--;while(len>1&&output[len-1]!='/')len--;}
            continue;
        }
        if(n>RIX_VFS_NAME_MAX)return -1;
        if(append_component(output,output_size,&len,s,n)!=0)return -1;
    }
    if(len>1&&output[len-1]=='/')len--;output[len]=0;return 0;
}

int vfs_init(void){root.node.inode=1;root.node.type=RIX_VFS_DIR;root.node.mode=0755;root.node.uid=0;root.node.gid=0;root.node.size=0;return 0;}
int vfs_root(rix_vfs_path_t *out){if(!out)return -1;out->node=&root.node;out->path[0]='/';out->path[1]=0;return 0;}
int vfs_lookup(const char *path,rix_vfs_path_t *out){if(!path||!out)return -1;if(vfs_normalize_path(path,out->path,sizeof(out->path))!=0)return -1;if(out->path[0]=='/'&&out->path[1]==0){out->node=&root.node;return 0;}return -1;}
