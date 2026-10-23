#define _GNU_SOURCE
#include "platform/host.h"
#include "platform/linux/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

extern char **environ;

int avxemu_host_identity(int self, avxemu_file_identity *identity) {
    avxemu_file_identity result = {0};
    int fd;
    struct stat st;
    if (!identity || !avxemu_linux_image_file(self, &fd, result.build_id) ||
        fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        st.st_mtim.tv_nsec < 0 || st.st_mtim.tv_nsec >= 1000000000) return 0;
    unsigned have_id = 0;
    for (size_t i = 0; i < sizeof result.build_id; i++) have_id |= result.build_id[i];
    if (self && !have_id) return 0;
    result.dev = (uint64_t)st.st_dev;
    result.ino = (uint64_t)st.st_ino;
    result.size = (uint64_t)st.st_size;
    result.mtime_s = (uint64_t)st.st_mtim.tv_sec;
    result.mtime_ns = (uint64_t)st.st_mtim.tv_nsec;
    *identity = result;
    return 1;
}

char **avxemu_host_environ(void) { return environ; }

int avxemu_host_cache_dir(char *dst, size_t size) {
    if (!dst || !size) return 0;
    const char *override = getenv("AVXEMU_CACHE_DIR");
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    int n;
    if (override && *override) n = snprintf(dst, size, "%s", override);
    else if (xdg && *xdg == '/') n = snprintf(dst, size, "%s/avxemu", xdg);
    else if (home && *home == '/') n = snprintf(dst, size, "%s/.cache/avxemu", home);
    else return 0;
    return n >= 0 && (size_t)n < size;
}

uint64_t avxemu_host_ticks(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
        (uint64_t)now.tv_sec > (UINT64_MAX - (uint64_t)now.tv_nsec) / 1000000000) return 0;
    return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}
