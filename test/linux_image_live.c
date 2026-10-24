#define _GNU_SOURCE
#include "image.h"
#include "platform/host.h"
#include "platform/linux/image.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
extern int elf_asm_probe(int);
extern char elf_asm_end[], elf_data_island[], elf_raw_probe[], elf_test_probe[];
int main(int argc, char **argv) {
    assert(argc == 2);
    assert(!avxemu_image_main());
    assert(avxemu_image_init() && avxemu_image_init());
    const avxemu_image *image = avxemu_image_main();
    assert(image && image->path && image->nranges && image->nfunctions);
    uint32_t r; size_t off;
    assert(elf_asm_probe(1) == 8);
    assert(avxemu_image_find(image, (uintptr_t)elf_asm_probe, 1, &r, &off));
    const avxemu_function *f = avxemu_image_function(image, (uintptr_t)elf_asm_probe);
    assert(f && f->range == r && f->begin == off);
    assert(f->end - f->begin == (uintptr_t)elf_asm_end - (uintptr_t)elf_asm_probe);
    assert(!avxemu_image_function(image, (uintptr_t)elf_data_island));
    assert(!avxemu_image_find(image, (uintptr_t)elf_raw_probe, 1, 0, 0));
    assert(!avxemu_image_find(image, (uintptr_t)elf_test_probe, 1, 0, 0));
    int embedded = !strcmp(argv[1], "embedded");
    if (embedded) assert(avxemu_image_find(image, (uintptr_t)avxemu_image_init, 1, 0, 0));
    avxemu_file_identity exe, self;
    assert(avxemu_host_identity(0, &exe) && avxemu_host_identity(1, &self));
    assert(exe.size && self.size);
    assert((exe.ino == self.ino && exe.dev == self.dev) == embedded);
    int fd = -1; uint8_t id[32]; memset(id, 0xa5, sizeof id);
    assert(!avxemu_linux_image_file(2, &fd, id) && fd == -1 && id[0] == 0xa5);
    assert(avxemu_linux_image_file(0, &fd, id));
    assert((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
    struct stat st; assert(!fstat(fd, &st));
    assert((uint64_t)st.st_ino == exe.ino && (uint64_t)st.st_dev == exe.dev);
    assert(!memcmp(id, exe.build_id, sizeof id));
    puts("linux-image-live: OK"); return 0;
}
