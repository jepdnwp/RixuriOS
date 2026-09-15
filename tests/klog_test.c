#include "../kernel/log/klog.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    /* Basic push + ordered read. */
    klog_push("hello ", 6);
    klog_push("world", 5);
    uint64_t seq = klog_write_seq();
    assert(seq == 11u);
    assert(klog_oldest() == 0u);
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    assert(klog_copy(0, buf, 11) == 11u);
    assert(memcmp(buf, "hello world", 11) == 0);
    printf("basic PASS\n");

    /* Cursor in the middle. */
    memset(buf, 0, sizeof(buf));
    assert(klog_copy(6, buf, 5) == 5u);
    assert(memcmp(buf, "world", 5) == 0);
    /* Past-end reads zero bytes. */
    assert(klog_copy(seq, buf, 5) == 0u);
    assert(klog_copy(seq + 100, buf, 5) == 0u);
    printf("cursor PASS\n");

    /* Fill + wrap: 3x ring size of patterned bytes. */
    for (unsigned r = 0; r < 3; r++) {
        uint8_t chunk[4096];
        for (size_t i = 0; i < sizeof(chunk); i++)
            chunk[i] = (uint8_t)('A' + (r % 26));
        for (unsigned k = 0; k < 16; k++)
            klog_push((const char *)chunk, sizeof(chunk));
    }
    uint64_t seq2 = klog_write_seq();
    assert(seq2 == 11u + 3u * 16u * 4096u);
    assert(klog_oldest() == seq2 - 65536u);
    /* Stale cursor clamps to oldest. */
    memset(buf, 0, sizeof(buf));
    assert(klog_copy(0, buf, sizeof(buf)) == sizeof(buf));
    /* Tail reads back the last pattern ('C' round). */
    uint8_t tail[16];
    assert(klog_copy(seq2 - sizeof(tail), tail, sizeof(tail)) == sizeof(tail));
    for (size_t i = 0; i < sizeof(tail); i++) assert(tail[i] == (uint8_t)'C');
    printf("wrap PASS oldest=%llu\n", (unsigned long long)klog_oldest());

    /* Empty push is a no-op. */
    uint64_t before = klog_write_seq();
    klog_push(NULL, 5);
    klog_push("x", 0);
    assert(klog_write_seq() == before);
    printf("klog tests: PASS\n");
    return 0;
}
