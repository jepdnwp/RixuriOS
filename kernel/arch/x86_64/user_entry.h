#pragma once
#include <stdint.h>
int x86_enter_user(uint64_t pml4_phys,uint64_t entry,uint64_t user_stack);
