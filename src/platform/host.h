#ifndef AVXEMU_PLATFORM_HOST_H
#define AVXEMU_PLATFORM_HOST_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t dev, ino, size, mtime_s, mtime_ns;
    uint8_t build_id[32];
} avxemu_file_identity;
/* Initialization only. self selects the emulator rather than the executable.
 * Failure leaves the output unchanged. */
int avxemu_host_identity(int self, avxemu_file_identity *identity);
char **avxemu_host_environ(void);
int avxemu_host_cache_dir(char *dst, size_t size);
/* Handler-safe diagnostic clock, in platform-specific monotonic units. */
uint64_t avxemu_host_ticks(void);
#endif
