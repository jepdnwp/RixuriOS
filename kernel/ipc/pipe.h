#pragma once
#include <stddef.h>
#include <stdint.h>
#include "channel.h"

typedef struct {
    rix_ipc_channel_t channel;
    uint8_t read_open;
    uint8_t write_open;
    uint8_t read_refs;
    uint8_t write_refs;
} rix_pipe_t;

void pipe_init(rix_pipe_t *pipe);
int pipe_retain_read(rix_pipe_t *pipe);
int pipe_retain_write(rix_pipe_t *pipe);
int pipe_read(rix_pipe_t *pipe, void *buffer, size_t capacity, size_t *readn);
int pipe_write(rix_pipe_t *pipe, const void *buffer, size_t length, size_t *written);
int pipe_close_read(rix_pipe_t *pipe);
int pipe_close_write(rix_pipe_t *pipe);
