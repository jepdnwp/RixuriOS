#include "pipe.h"

void pipe_init(rix_pipe_t *pipe) {
    if (!pipe) return;
    ipc_channel_init(&pipe->channel);
    pipe->read_open = 1u;
    pipe->write_open = 1u;
}

int pipe_read(rix_pipe_t *pipe, void *buffer, size_t capacity, size_t *readn) {
    if (readn) *readn = 0;
    if (!pipe || !pipe->read_open) return -1;
    return ipc_read(&pipe->channel, buffer, capacity, readn);
}

int pipe_write(rix_pipe_t *pipe, const void *buffer, size_t length, size_t *written) {
    if (written) *written = 0;
    if (!pipe || !pipe->write_open || !pipe->read_open) return -1;
    return ipc_write(&pipe->channel, buffer, length, written);
}

int pipe_close_read(rix_pipe_t *pipe) {
    if (!pipe || !pipe->read_open) return -1;
    pipe->read_open = 0u;
    return ipc_close(&pipe->channel);
}

int pipe_close_write(rix_pipe_t *pipe) {
    if (!pipe || !pipe->write_open) return -1;
    pipe->write_open = 0u;
    return ipc_close(&pipe->channel);
}
