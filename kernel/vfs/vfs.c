#include "vfs.h"
#include <stddef.h>
typedef struct { const char *name; } vfs_root_t;
static vfs_root_t root;
int vfs_init(void){root.name="/";return root.name?0:-1;}
