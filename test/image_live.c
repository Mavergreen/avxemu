#include "image.h"
#include "platform/host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void avxemu_cpuid_raw(uint32_t, uint32_t, uint32_t[4]);
__attribute__((noinline)) static int probe(int x) { return x + 3; }
__attribute__((noinline, section("__TEXT,__avxemu_test,regular,pure_instructions")))
static int test_section(int x) { return x + 4; }

int main(int argc, char **argv) {
    assert(argc == 2 && avxemu_image_init());
    const avxemu_image *image = avxemu_image_main();
    assert(image && image->path && image->nranges == 1 && image->nfunctions);
    uint32_t r; size_t off;
    assert(probe(argc) == 5 && test_section(argc) == 6);
    assert(avxemu_image_find(image, (uintptr_t)&probe, 1, &r, &off));
    assert(avxemu_image_function(image, (uintptr_t)&probe));
    assert(!avxemu_image_find(image, (uintptr_t)&test_section, 1, 0, 0));
    assert(!avxemu_image_find(image, (uintptr_t)&avxemu_cpuid_raw, 1, 0, 0));
    int embedded = !strcmp(argv[1], "embedded");
    assert(avxemu_image_find(image, (uintptr_t)&avxemu_image_init, 1, 0, 0) == embedded);
    avxemu_file_identity self, exe;
    assert(avxemu_host_identity(0, &exe) && avxemu_host_identity(1, &self));
    assert(exe.size && self.size && self.build_id[31] == 16);
    if (embedded) assert(exe.ino == self.ino && exe.dev == self.dev);
    else assert(exe.ino != self.ino || exe.dev != self.dev);
    assert(avxemu_host_environ());
    char dir[1024]; assert(avxemu_host_cache_dir(dir, sizeof dir) && dir[0]);
    assert(!avxemu_host_cache_dir(dir, 1));
    assert(avxemu_host_ticks());
    puts("image-live: OK");
    return 0;
}
