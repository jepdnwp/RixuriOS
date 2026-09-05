#pragma once
#include <stdint.h>
#include "../process/process.h"

#define RIX_SHM_MAX 64
#define RIX_SHM_MAX_PAGES 256
#define RIX_SHM_NAME_MAX 32

typedef uint32_t rix_shm_id_t;

int shm_init(void);
int shm_create(uint64_t size, rix_shm_id_t *out_id);
int shm_map(rix_shm_id_t id, pid_t pid, uint64_t va, uint64_t flags);
int shm_unmap(rix_shm_id_t id, pid_t pid, uint64_t va);
int shm_destroy(rix_shm_id_t id);
uint64_t shm_size(rix_shm_id_t id);
