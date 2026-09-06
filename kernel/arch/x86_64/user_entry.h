#pragma once
#include <stdint.h>
__attribute__((noreturn)) void x86_enter_user(uint64_t pml4_phys,uint64_t entry,uint64_t user_stack);
__attribute__((noreturn)) void x86_enter_user_return(uint64_t pml4_phys,uint64_t entry,uint64_t user_stack,uint64_t return_value);
