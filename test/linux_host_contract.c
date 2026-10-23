#define _GNU_SOURCE
#undef NDEBUG
#include "platform/host.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform/linux/image.h"

/* Fixture metadata only: production image/file verification belongs to the
 * Linux image backend. The host must stat this borrowed descriptor itself. */
static int image_fd = -1;
static int have_id = 1;
int avxemu_linux_image_file(int self, int *fd, uint8_t build_id[32]) {
    (void)self;
    if (image_fd < 0) return 0;
    *fd = image_fd;
    memset(build_id, 0, 32);
    if (have_id) { build_id[0] = 0x42; build_id[31] = 20; }
    return 1;
}

static void cache_path(const char *expected) {
    char path[512];
    assert(avxemu_host_cache_dir(path, sizeof path));
    assert(!strcmp(path, expected));
}

int main(void) {
    assert(!unsetenv("AVXEMU_CACHE_DIR"));
    assert(!setenv("HOME", "/fixture/home", 1));
    assert(!setenv("XDG_CACHE_HOME", "/fixture/cache", 1));
    cache_path("/fixture/cache/avxemu");
    assert(!setenv("XDG_CACHE_HOME", "relative/cache", 1));
    cache_path("/fixture/home/.cache/avxemu");
    assert(!unsetenv("HOME"));
    char path[512];
    assert(!avxemu_host_cache_dir(path, sizeof path));
    assert(!setenv("XDG_CACHE_HOME", "/absolute", 1));
    cache_path("/absolute/avxemu");
    assert(!avxemu_host_cache_dir(path, 2));
    assert(!avxemu_host_cache_dir(NULL, sizeof path));
    assert(!setenv("AVXEMU_CACHE_DIR", "/explicit", 1));
    cache_path("/explicit");
    assert(!setenv("AVXEMU_CACHE_DIR", "", 1));
    assert(!setenv("HOME", "relative", 1));
    assert(!unsetenv("XDG_CACHE_HOME"));
    assert(!avxemu_host_cache_dir(path, sizeof path));
    assert(avxemu_host_environ());
    assert(!setenv("AVXEMU_HOST_TEST", "visible", 1));
    int found = 0;
    for (char **p = avxemu_host_environ(); *p; p++)
        if (!strcmp(*p, "AVXEMU_HOST_TEST=visible")) found = 1;
    assert(found);
    uint64_t before = avxemu_host_ticks();
    assert(before && avxemu_host_ticks() >= before);

    avxemu_file_identity identity, original;
    memset(&identity, 0xa5, sizeof identity);
    original = identity;
    assert(!avxemu_host_identity(0, &identity));
    assert(!memcmp(&identity, &original, sizeof identity));
    char name[] = "/tmp/avxemu-host-XXXXXX";
    image_fd = mkstemp(name);
    assert(image_fd >= 0);
    assert(!unlink(name));
    assert(write(image_fd, "fixture", 7) == 7);
    struct timespec times[2] = {{123, 456}, {987654321, 123456789}};
    assert(!futimens(image_fd, times));
    /* The fixture is already unlinked; obtain a read-only descriptor through
     * procfs just as a caller could, without retaining a writable handle. */
    char fdpath[64];
    assert(snprintf(fdpath, sizeof fdpath, "/proc/self/fd/%d", image_fd) > 0);
    int readonly = open(fdpath, O_RDONLY | O_CLOEXEC);
    assert(readonly >= 0);
    assert(!close(image_fd));
    image_fd = readonly;
    struct stat st;
    assert(!fstat(image_fd, &st));
    assert(avxemu_host_identity(0, &identity));
    assert(identity.dev == (uint64_t)st.st_dev && identity.ino == (uint64_t)st.st_ino);
    assert(identity.size == 7 && identity.mtime_s == 987654321 && identity.mtime_ns == 123456789);
    assert(identity.build_id[0] == 0x42 && identity.build_id[31] == 20);
    have_id = 0;
    assert(avxemu_host_identity(0, &identity));
    identity = original;
    assert(!avxemu_host_identity(1, &identity));
    assert(!memcmp(&identity, &original, sizeof identity));
    assert(!close(image_fd));
    assert(!avxemu_host_identity(0, &identity));
    assert(!memcmp(&identity, &original, sizeof identity));
    puts("linux-host-contract: all checks passed");
}
