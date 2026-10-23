#define _DARWIN_C_SOURCE
#include "../host.h"
#include "image.h"
#include <crt_externs.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int avxemu_host_identity(int self, avxemu_file_identity *identity) {
    avxemu_file_identity result = {0};
    const char *path = 0;
    int uuid = avxemu_darwin_image_uuid(self, result.build_id, &path);
    /* The executable may have its UUID stripped to make load-command room.
     * The emulator's build identity is required, even when stat succeeds. */
    if (!identity || (self && !uuid)) return 0;
    struct stat st;
    if (!path || stat(path, &st) != 0) return 0;
    result.dev = (uint64_t)st.st_dev; result.ino = (uint64_t)st.st_ino;
    result.size = (uint64_t)st.st_size;
    result.mtime_s = (uint64_t)st.st_mtimespec.tv_sec;
    result.mtime_ns = (uint64_t)st.st_mtimespec.tv_nsec;
    if (uuid) {
        /* UUID bytes plus their domain and length normalize into 32 bytes. */
        memcpy(result.build_id + 16, "Mach-O UUID", 11);
        result.build_id[31] = 16;
    }
    *identity = result; return 1;
}

char **avxemu_host_environ(void) { return *_NSGetEnviron(); }

int avxemu_host_cache_dir(char *dst, size_t size) {
    if (!dst || !size) return 0;
    const char *override = getenv("AVXEMU_CACHE_DIR");
    const char *home = getenv("HOME");
    int n;
    if (override && *override) n = snprintf(dst, size, "%s", override);
    else if (home && *home) n = snprintf(dst, size, "%s/Library/Caches/avxemu", home);
    else return 0;
    return n >= 0 && (size_t)n < size;
}

uint64_t avxemu_host_ticks(void) { return mach_absolute_time(); }
