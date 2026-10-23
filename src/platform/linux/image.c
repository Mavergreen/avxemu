#define _GNU_SOURCE
#include "image.h"
#include "elf.h"
#include "maps.h"
#include "../../image.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    int fd;
    char *path;
    uint8_t build_id[32];
} image_file;
static image_file main_file = {-1, 0, {0}}, self_file = {-1, 0, {0}};
static avxemu_image main_image;
static int ready;

typedef struct {
    struct link_map *main;
    int self, found;
    uintptr_t bias;
    Elf64_Phdr *phdr;
    size_t phnum;
    char *path;
} selection;

static int select_loaded(struct dl_phdr_info *info, size_t size, void *arg) {
    (void)size;
    selection *s = arg; int match = 0;
    uintptr_t me = (uintptr_t)&select_loaded;
    for (size_t i = 0; i < info->dlpi_phnum; i++) {
        const Elf64_Phdr *p = &info->dlpi_phdr[i];
        if (p->p_vaddr > UINTPTR_MAX - info->dlpi_addr) continue;
        uintptr_t begin = info->dlpi_addr + p->p_vaddr;
        if (s->self && p->p_type == PT_LOAD && me >= begin && me - begin < p->p_memsz) match = 1;
        if (!s->self && p->p_type == PT_DYNAMIC && s->main &&
            s->main->l_addr == info->dlpi_addr && (uintptr_t)s->main->l_ld == begin) match = 1;
    }
    if (!match) return 0;
    s->phnum = info->dlpi_phnum;
    s->phdr = malloc(s->phnum * sizeof *s->phdr);
    s->path = strdup(info->dlpi_name ? info->dlpi_name : "");
    if (!s->phdr || !s->path) return 1;
    memcpy(s->phdr, info->dlpi_phdr, s->phnum * sizeof *s->phdr);
    s->bias = info->dlpi_addr; s->found = 1; return 1;
}

/* File metadata is accepted only if the loader's actual mappings name this
 * device/inode at the corresponding file offsets. This also distinguishes a
 * directly invoked ld.so (/proc/self/exe) from the executable it loaded. */
static int mapping_identity(uintptr_t address, avxemu_map_file *file, avxemu_mapping *mapping) {
    maps_reader reader; avxemu_mapping m; avxemu_map_file f;
    if (!avxemu_maps_open(&reader)) return 0;
    int status;
    while ((status = avxemu_maps_next(&reader, &m, &f)) == 1) {
        if (address < m.begin) { status = 0; break; }
        if (address < m.end) break;
    }
    if (close(reader.fd) || status != 1 || !f.ino) return 0;
    *file = f; if (mapping) *mapping = m; return 1;
}
static int mapped_file(uintptr_t address, size_t n, uint64_t offset,
                       const avxemu_map_file *identity, unsigned required) {
    maps_reader reader; avxemu_mapping m; avxemu_map_file f;
    if (n > UINTPTR_MAX - address || !avxemu_maps_open(&reader)) return 0;
    while (n && avxemu_maps_next(&reader, &m, &f) == 1) {
        if (address < m.begin) break;
        if (address >= m.end) continue;
        if (f.major != identity->major || f.minor != identity->minor || f.ino != identity->ino ||
            address - m.begin > UINT64_MAX - f.offset || f.offset + (address - m.begin) != offset ||
            (m.prot & required) != required) break;
        size_t part = m.end - address;
        if (part > n) part = n;
        address += part; offset += part; n -= part;
    }
    return close(reader.fd) == 0 && n == 0;
}
static int same_stat(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
        a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
static int prepare(int self, image_file *file, avxemu_elf *elf, uintptr_t *bias) {
    selection s = {0}; s.self = self;
    void *probe = MAP_FAILED;
    void *handle = 0; int fd = -1, ok = 0; uint8_t *data = 0; avxemu_elf e = {0};
    if (!self) {
        handle = dlopen(0, RTLD_NOW);
        if (!handle || dlinfo(handle, RTLD_DI_LINKMAP, &s.main)) goto done;
    }
    dl_iterate_phdr(select_loaded, &s);
    if (!s.found) goto done;
    if (!*s.path) {
        avxemu_mapping m; avxemu_map_file identity;
        /* glibc may leave the main link-map name empty even when ld.so was
         * invoked explicitly. Resolve its actual mapped pathname, not argv. */
        uintptr_t anchor = 0;
        for (size_t i = 0; i < s.phnum; i++)
            if (s.phdr[i].p_type == PT_LOAD && s.phdr[i].p_filesz &&
                s.phdr[i].p_vaddr <= UINTPTR_MAX - s.bias) {
                anchor = s.bias + s.phdr[i].p_vaddr; break;
            }
        if (!anchor || !mapping_identity(anchor, &identity, &m)) goto done;
        char link[96], path[4096];
        int len = snprintf(link, sizeof link, "/proc/self/map_files/%lx-%lx",
                           (unsigned long)m.begin, (unsigned long)m.end);
        if (len < 0 || (size_t)len >= sizeof link) goto done;
        ssize_t n = readlink(link, path, sizeof path - 1);
        if (n <= 0 || (size_t)n >= sizeof path - 1) goto done;
        path[n] = 0; free(s.path); s.path = strdup(path);
        if (!s.path) goto done;
    }
    fd = open(s.path, O_RDONLY | O_CLOEXEC);
    /* Deleted main executables remain available through this link. The
     * mapping comparison below still rejects a different loader executable. */
    if (fd < 0 && !self) fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    struct stat st, after;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > SIZE_MAX || (uint64_t)st.st_size > PTRDIFF_MAX) goto done;
    /* Overlay filesystems may translate st_dev. Compare kernel mapping
     * identities on both sides instead of weakening the device check. */
    probe = mmap(0, 1, PROT_READ, MAP_PRIVATE, fd, 0);
    avxemu_map_file identity;
    if (probe == MAP_FAILED || !mapping_identity((uintptr_t)probe, &identity, 0)) goto done;
    size_t size = (size_t)st.st_size;
    data = malloc(size);
    if (!data) goto done;
    size_t got = 0;
    while (got < size) {
        size_t n = size - got; if (n > 1048576) n = 1048576;
        ssize_t r = pread(fd, data + got, n, (off_t)got);
        if (r <= 0) goto done;
        got += (size_t)r;
    }
    avxemu_elf_view view = {data, size, 0};
    if (!avxemu_elf_parse(&view, &e) || e.header.e_phnum != s.phnum ||
        memcmp(data + e.header.e_phoff, s.phdr, s.phnum * sizeof *s.phdr)) goto done;
    for (size_t i = 0; i < e.nloads; i++) {
        const Elf64_Phdr *p = &e.loads[i];
        if (p->p_vaddr > UINTPTR_MAX - s.bias || p->p_memsz > UINTPTR_MAX - (s.bias + p->p_vaddr)) goto done;
        if (p->p_filesz && !mapped_file(s.bias + p->p_vaddr, p->p_filesz, p->p_offset, &identity, 0)) goto done;
    }
    for (size_t i = 0; i < e.nranges; i++) {
        const avxemu_elf_range *r = &e.ranges[i];
        if (!mapped_file(s.bias + r->begin, r->end - r->begin, r->offset, &identity, AVXEMU_PROT_R | AVXEMU_PROT_X)) goto done;
        /* A modified file must not authorize boundaries for different loaded
         * code. Compare before any rewriting, using the fault-tolerant reader. */
        uint8_t bytes[4096]; uint64_t off = 0;
        while (off < r->end - r->begin) {
            size_t n = r->end - r->begin - off; if (n > sizeof bytes) n = sizeof bytes;
            if (avxemu_memory_read(bytes, s.bias + r->begin + off, n) != n ||
                memcmp(bytes, data + r->offset + off, n)) goto done;
            off += n;
        }
    }
    if (fstat(fd, &after) || !same_stat(&st, &after)) goto done;
    if (munmap(probe, 1)) goto done;
    probe = MAP_FAILED;
    file->fd = fd; fd = -1; file->path = s.path; s.path = 0;
    memcpy(file->build_id, e.build_id, sizeof file->build_id);
    *elf = e; memset(&e, 0, sizeof e); *bias = s.bias; ok = 1;
done:
    if (probe != MAP_FAILED) munmap(probe, 1);
    if (handle) dlclose(handle);
    if (fd >= 0) close(fd);
    free(data); free(s.phdr); free(s.path); avxemu_elf_destroy(&e); return ok;
}

int avxemu_image_init(void) {
    if (ready) return 1;
    image_file file = {-1, 0, {0}}; avxemu_elf e = {0}; uintptr_t bias;
    avxemu_code_range *ranges = 0; avxemu_function *functions = 0;
    if (!prepare(0, &file, &e, &bias) || e.nranges > UINT32_MAX || e.nfunctions > UINT32_MAX) goto fail;
    ranges = calloc(e.nranges, sizeof *ranges);
    if (e.nfunctions) functions = calloc(e.nfunctions, sizeof *functions);
    if (!ranges || (e.nfunctions && !functions)) goto fail;
    for (size_t i = 0; i < e.nranges; i++) {
        const avxemu_elf_range *r = &e.ranges[i];
        ranges[i] = (avxemu_code_range){(uint8_t *)(bias + r->begin), r->begin,
                                       r->end-r->begin, r->end-r->begin, r->prot};
    }
    size_t r = 0;
    for (size_t i = 0; i < e.nfunctions; i++) {
        const avxemu_elf_bound *f = &e.functions[i];
        while (r < e.nranges && f->begin >= e.ranges[r].end) r++;
        if (r == e.nranges || f->begin < e.ranges[r].begin || f->end > e.ranges[r].end) goto fail;
        functions[i] = (avxemu_function){(uint32_t)r, f->begin-e.ranges[r].begin, f->end-e.ranges[r].begin};
    }
    main_file = file;
    main_image = (avxemu_image){ranges, e.nranges, functions, e.nfunctions, file.path};
    avxemu_elf_destroy(&e); ready = 1; return 1;
fail:
    if (file.fd >= 0) close(file.fd);
    free(file.path); free(ranges); free(functions); avxemu_elf_destroy(&e); return 0;
}
const avxemu_image *avxemu_image_main(void) { return ready ? &main_image : 0; }
int avxemu_linux_image_file(int self, int *fd, uint8_t build_id[32]) {
    if (!fd || !build_id || (self != 0 && self != 1) || !avxemu_image_init()) return 0;
    image_file *file = &main_file;
    if (self) {
        file = &self_file;
        if (file->fd < 0) {
            avxemu_elf e; uintptr_t bias;
            if (!prepare(1, file, &e, &bias)) return 0;
            avxemu_elf_destroy(&e);
        }
    }
    *fd = file->fd; memcpy(build_id, file->build_id, sizeof file->build_id); return 1;
}
