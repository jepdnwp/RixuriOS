#include "process.h"
static pid_t current_pid;
int process_init(void){current_pid=0;return 0;}
pid_t process_current(void){return current_pid;}
