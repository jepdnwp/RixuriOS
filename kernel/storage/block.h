#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_BLOCK_NAME_MAX 32
#define RIX_BLOCK_MAX_DEVICES 32
#define RIX_BIO_MAX_SECTORS 128

typedef enum { RIX_BIO_READ=0, RIX_BIO_WRITE=1, RIX_BIO_FLUSH=2 } rix_bio_op_t;
typedef enum { RIX_BIO_PENDING=0, RIX_BIO_COMPLETE=1, RIX_BIO_ERROR=2, RIX_BIO_TIMEOUT=3 } rix_bio_state_t;

typedef struct rix_block_device rix_block_device_t;
typedef struct { rix_bio_op_t op; uint64_t sector; uint32_t count; void *buffer; size_t buffer_size; rix_bio_state_t state; int error; } rix_bio_t;
typedef int (*rix_block_submit_fn)(rix_block_device_t *,rix_bio_t *);

struct rix_block_device { char name[RIX_BLOCK_NAME_MAX]; uint32_t sector_size; uint64_t sector_count; uint32_t max_sectors; uint32_t flags; rix_block_submit_fn submit; void *driver_data; };

int block_init(void);
int block_register(rix_block_device_t *device);
rix_block_device_t *block_find(const char *name);
int block_submit(rix_block_device_t *device,rix_bio_t *bio);
size_t block_device_count(void);
