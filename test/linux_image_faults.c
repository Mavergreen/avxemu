#define _GNU_SOURCE
#include "image.h"
#include "platform/linux/image.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int mode, probes;
static const char *replacement;
int __real_open(const char *, int, ...);
void *__real_mmap(void *, size_t, int, int, int, off_t);
int __real_munmap(void *, size_t);
int __wrap_open(const char *path, int flags, ...) {
    if (!strcmp(path, "/proc/self/maps")) {
        if (mode == 3 && probes) { errno = EACCES; return -1; }
        return __real_open(path, flags);
    }
    if (mode == 1) return __real_open(replacement, flags);
    return __real_open(path, flags);
}
void *__wrap_mmap(void *p, size_t n, int prot, int flags, int fd, off_t off) {
    if (fd >= 0 && mode == 2) { errno = ENOMEM; return MAP_FAILED; }
    void *result = __real_mmap(p,n,prot,flags,fd,off);
    if (fd >= 0 && result != MAP_FAILED) probes++;
    return result;
}
int __wrap_munmap(void *p, size_t n) {
    int result = __real_munmap(p,n); if (!result) probes--; return result;
}
static unsigned descriptors(void) {
    DIR *dir = opendir("/proc/self/fd"); assert(dir); unsigned n = 0;
    while (readdir(dir)) n++;
    closedir(dir); return n;
}
int main(int argc, char **argv) {
    assert(argc == 3); mode = atoi(argv[1]); replacement = argv[2];
    char copy[] = "/tmp/avxemu-image-XXXXXX";
    if (mode == 1) {
        int src = __real_open("/proc/self/exe", O_RDONLY), dst = mkstemp(copy);
        assert(src >= 0 && dst >= 0); char bytes[4096]; ssize_t n;
        while ((n = read(src, bytes, sizeof bytes)) > 0) assert(write(dst, bytes, (size_t)n) == n);
        assert(n == 0 && !close(src) && !close(dst)); replacement = copy;
    }
    unsigned before = descriptors();
    for (unsigned i=0;i<3;i++) {
        assert(!avxemu_image_init() && !avxemu_image_main());
        int fd = -7; uint8_t id[32]; memset(id,0x5a,sizeof id);
        assert(!avxemu_linux_image_file(0,&fd,id) && fd == -7 && id[0] == 0x5a);
        assert(probes == 0 && descriptors() == before);
    }
    if (mode == 1) assert(!unlink(copy));
    puts("linux-image-faults: OK"); return 0;
}
