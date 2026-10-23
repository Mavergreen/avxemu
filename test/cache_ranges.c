/* Cache format and replay contract with two independent executable ranges. */
#define _DARWIN_C_SOURCE
#define _GNU_SOURCE
#include "lcache.h"
#include "avxemu_cpuid.h"
#include "lde.h"
#include "platform/host.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static avxemu_image image;
static int identity_change;
static char *settings[] = {"AVXEMU_ALPHA=1", "AVXEMU_BETA=2", 0};
static char directory[512], path[1024];
const avxemu_image *avxemu_image_main(void) { return &image; }
int avxemu_host_identity(int self, avxemu_file_identity *id) {
    memset(id, 0, sizeof *id); id->ino = self ? 2 : 1;
    if (identity_change == 11 && self) id->dev = 1;
    if (identity_change == 12 && !self) id->mtime_ns = 1;
    return 1;
}
char **avxemu_host_environ(void) { return settings; }
int avxemu_host_cache_dir(char *dst, size_t n) {
    int k = snprintf(dst, n, "%s", directory); return k >= 0 && (size_t)k < n;
}
static uint32_t checksum(const void *p, size_t n) {
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    const uint8_t *b = p;
    while (n--) { h ^= *b++; h *= UINT64_C(0x100000001b3); }
    return (uint32_t)h;
}
static void locate(void) {
    DIR *d = opendir(directory); assert(d);
    struct dirent *e; int found = 0;
    while ((e = readdir(d))) if (strstr(e->d_name, ".avxc")) {
        assert(snprintf(path, sizeof path, "%s/%s", directory, e->d_name) < (int)sizeof path); found++;
    }
    closedir(d); assert(found == 1);
}
static void mutate(int which) {
    int fd = open(path, O_RDWR); assert(fd >= 0);
    struct stat st; assert(!fstat(fd, &st));
    uint8_t *data = malloc((size_t)st.st_size); assert(data);
    assert(read(fd, data, (size_t)st.st_size) == st.st_size);
    lc_hdr *h = (lc_hdr *)data;
    if (which == 0) h->version = 1;
    if (which == 1) h->n_bytes = UINT32_MAX;
    if (which == 15) ((lc_byte *)(data + sizeof *h))[1].reserved[5] = 1;
    if (which == 16) ((lc_byte *)(data + sizeof *h))[1].site = ((lc_byte *)(data + sizeof *h))[0].site;
    h->body_sum = checksum(data + sizeof *h, (size_t)st.st_size - sizeof *h);
    assert(lseek(fd, 0, SEEK_SET) == 0);
    assert(write(fd, data, (size_t)st.st_size) == st.st_size);
    assert(!close(fd)); free(data);
}
static void fixture(int which) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *mem = mmap(0, page * 3, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(mem != MAP_FAILED); memset(mem, 0x90, page * 3);
    mem[0] = mem[2 * page] = 0x0f; mem[1] = mem[2 * page + 1] = 0xa2;
    avxemu_code_range ranges[] = {
        {mem, 0x1000, 16, 16, AVXEMU_PROT_R | AVXEMU_PROT_X},
        {mem + 2 * page, 0x4000, 16, 16, AVXEMU_PROT_R | AVXEMU_PROT_X}
    };
    image.ranges = ranges; image.nranges = 2;
    char root[] = "/tmp/avxemu-ranges.XXXXXX"; assert(mkdtemp(root));
    assert(snprintf(directory, sizeof directory, "%s/absent/Library/Caches/avxemu", root) < (int)sizeof directory);
    unsetenv("AVXEMU_NOCACHE");
    avxemu_lc_begin(); assert(avxemu_lc_mode == LC_RECORD);
    lc_site first = {0, 0, 4}, second = {1, 0, 4};
    if (which == 2) second.off = 16;        /* offset at end */
    if (which == 3) second.range = 2;      /* unknown range */
    if (which == 4) second.off = UINT64_MAX;
    if (which == 5) {
        lc_site start = {0, 0, 0}, resume = {1, 0, 8};
        avxemu_lc_rec_run(start, 1, resume);
    } else if (which == 8) {
        lc_site a = {0, 0, 0}, b = {1, 0, 0};
        avxemu_lc_rec_cpuid(a); avxemu_lc_rec_cpuid(b);
        mem[2 * page + 1] = 0x90; /* final CPUID disagrees */
    } else {
        avxemu_lc_rec_byte(first, 0x90, 0xf0);
        avxemu_lc_rec_byte(second, which == 7 ? 0x91 : 0x90, 0xf0);
    }
    avxemu_lc_end(); locate();
    if (which <= 1 || which >= 15) mutate(which);
    if (which == 9) {
        uint8_t *moved = mmap(0, page * 3, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANON, -1, 0);
        assert(moved != MAP_FAILED && moved != mem);
        memcpy(moved, mem, page * 3);
        assert(!munmap(mem, page * 3)); mem = moved;
        ranges[0].base = mem; ranges[1].base = mem + 2 * page;
    }
    if (which == 10) ranges[1].vaddr++;
    if (which == 11 || which == 12) identity_change = which;
    if (which == 13) { char *s = settings[0]; settings[0] = settings[1]; settings[1] = s; }
    if (which == 14) settings[1] = "AVXEMU_BETA=3";
    assert(!mprotect(mem, page * 3, PROT_READ | PROT_EXEC));
    avxemu_lc_begin();
    if (which <= 5 || (which >= 10 && which != 13)) {
        assert(avxemu_lc_mode == LC_RECORD); /* malformed files are misses */
        avxemu_lc_rec_fail();
    } else {
        assert(avxemu_lc_mode == LC_REPLAY);
        if (which == 8) {
            assert(avxemu_cpuid_patch_image(&image, 1) == 0);
            assert(!avxemu_cpuid_trap((uintptr_t)mem));
            assert(!avxemu_cpuid_trap((uintptr_t)(mem + 2 * page)));
            assert(mem[1] == 0xa2); /* first record was never published */
        } else {
            long n = avxemu_patch_zcnt_image(&image, 1, 1);
            int valid = which == 6 || which == 9 || which == 13;
            assert(n == (valid ? 2 : 0));
            assert(mem[4] == (valid ? 0xf0 : 0x90));
            assert(mem[2 * page + 4] == (valid ? 0xf0 : 0x90));
        }
        if (which == 7 || which == 8) assert(avxemu_lc_mode == LC_OFF);
    }
    assert(mem[0] == 0x0f && mem[2 * page] == 0x0f);
    if (which <= 5 || (which >= 10 && which != 13)) {
        assert(mem[4] == 0x90 && mem[2 * page + 4] == 0x90);
        assert(!avxemu_cpuid_trap((uintptr_t)mem) && !avxemu_cpuid_trap((uintptr_t)(mem + 2 * page)));
    }
    avxemu_lc_end(); munmap(mem, page * 3);
    unlink(path); rmdir(directory);
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); assert(system(cmd) == 0);
}
int main(void) {
    static const char *names[] = {"v1", "overflow count", "offset at end", "invalid range",
        "offset overflow", "cross-range resume", "range 1 replay", "final byte invalid", "final CPUID invalid", "different load addresses",
        "changed range layout", "changed emulator identity", "changed executable identity",
        "reordered settings", "changed settings", "reserved byte", "duplicate byte site"};
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        pid_t pid = fork(); assert(pid >= 0);
        if (!pid) { fixture(i); _exit(0); }
        int status; assert(waitpid(pid, &status, 0) == pid);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "cache-ranges: %s failed\n", names[i]); return 1;
        }
    }
    puts("cache-ranges: OK (17 fixtures)"); return 0;
}
