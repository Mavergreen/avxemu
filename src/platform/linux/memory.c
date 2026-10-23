#define _GNU_SOURCE
#include "platform/memory.h"
#include "maps.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static _Atomic size_t page_size;

size_t avxemu_page_size(void) {
    size_t page = atomic_load_explicit(&page_size, memory_order_relaxed);
    if (!page) {
        long value = sysconf(_SC_PAGESIZE);
        if (value > 0) {
            page = (size_t)value;
            atomic_store_explicit(&page_size, page, memory_order_relaxed);
        }
    }
    return page;
}

int avxemu_memory_query(uintptr_t address, avxemu_mapping *mapping) {
    maps_reader reader;
    if (!mapping || !avxemu_maps_open(&reader)) return -1;
    avxemu_mapping found;
    int status;
    while ((status = avxemu_maps_next(&reader, &found, 0)) == 1) {
        if (address < found.begin) { status = 0; break; }
        if (address < found.end) break;
    }
    if (close(reader.fd) != 0) return -1;
    if (status == 1) *mapping = found;
    return status;
}

size_t avxemu_memory_read(void *dst, uintptr_t src, size_t size) {
    size_t page = avxemu_page_size(), done = 0;
    uintptr_t target = (uintptr_t)dst;
    if (!page || size > UINTPTR_MAX - src || size > UINTPTR_MAX - target) return 0;
    while (done < size) {
        size_t n = page - (src + done) % page;
        if (n > size - done) n = size - done;
        struct iovec local = {(void *)(target + done), n};
        struct iovec remote = {(void *)(src + done), n};
        ssize_t got;
        do {
            got = syscall(SYS_process_vm_readv, getpid(), &local, 1UL, &remote, 1UL, 0UL);
        } while (got < 0 && errno == EINTR);
        /* ENOSYS/EPERM are failures too; never dereference the input instead. */
        if (got <= 0 || (size_t)got > n) break;
        done += (size_t)got;
        if ((size_t)got != n) break;
    }
    return done;
}

static int page_span(uintptr_t address, size_t size, uintptr_t *begin, uintptr_t *end) {
    size_t page = avxemu_page_size();
    if (!page || !size || size > UINTPTR_MAX - address) return 0;
    uintptr_t limit = address + size;
    size_t padding = limit % page ? page - limit % page : 0;
    if (padding > UINTPTR_MAX - limit) return 0;
    *begin = address - address % page;
    *end = limit + padding;
    return 1;
}

static int native_prot(unsigned prot) {
    return ((prot & AVXEMU_PROT_R) ? PROT_READ : 0) |
           ((prot & AVXEMU_PROT_W) ? PROT_WRITE : 0) |
           ((prot & AVXEMU_PROT_X) ? PROT_EXEC : 0);
}

int avxemu_memory_protect(uintptr_t address, size_t size, unsigned prot) {
    uintptr_t begin, end;
    if (prot & ~(AVXEMU_PROT_R | AVXEMU_PROT_W | AVXEMU_PROT_X)) return 0;
    if (!page_span(address, size, &begin, &end)) return 0;
    return mprotect((void *)begin, end - begin, native_prot(prot)) == 0;
}

int avxemu_memory_patch_begin(uintptr_t address, size_t size, avxemu_mapping *saved) {
    avxemu_mapping mapping;
    uintptr_t begin, end;
    if (!saved || !page_span(address, size, &begin, &end)) return 0;
    if (avxemu_memory_query(address, &mapping) != 1 || begin < mapping.begin || end > mapping.end) return 0;
    if (!avxemu_memory_protect(begin, end - begin, mapping.prot | AVXEMU_PROT_W)) return 0;
    *saved = (avxemu_mapping){begin, end, mapping.prot};
    return 1;
}

int avxemu_memory_patch_end(const avxemu_mapping *saved) {
    size_t page = avxemu_page_size();
    if (!saved || !page || saved->begin >= saved->end || saved->begin % page || saved->end % page) return 0;
    return avxemu_memory_protect(saved->begin, saved->end - saved->begin, saved->prot);
}

static void *map_exact(uintptr_t address, size_t size, int prot) {
    void *p = mmap((void *)address, size, prot,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return NULL;
    /* Older kernels may ignore NOREPLACE and treat the address as a hint. */
    if ((uintptr_t)p != address) { munmap(p, size); return NULL; }
    return p;
}

int avxemu_memory_map_gap(uintptr_t address, size_t size) {
    size_t page = avxemu_page_size();
    if (!page || !address || !size || address % page || size % page || size > UINTPTR_MAX - address) return 0;
    avxemu_mapping mapping;
    if (avxemu_memory_query(address, &mapping) != 0) return 0;
    return map_exact(address, size, PROT_READ | PROT_WRITE) != NULL;
}

void *avxemu_memory_alloc_near(uintptr_t site, size_t size) {
    uintptr_t begin, end;
    if (!page_span(0, size, &begin, &end)) return NULL;
    size = end;
    size_t page = avxemu_page_size();
    const uintptr_t reach = INT32_MAX - 5;
    uintptr_t low = site > reach ? site - reach : page;
    uintptr_t high = site <= UINTPTR_MAX - reach ? site + reach : UINTPTR_MAX;
    size_t padding = low % page ? page - low % page : 0;
    if (padding > UINTPTR_MAX - low) return NULL;
    low += padding;
    if (low > high || size - 1 > high - low) return NULL;
    maps_reader reader;
    if (!avxemu_maps_open(&reader)) return NULL;
    avxemu_mapping mapping;
    uintptr_t candidate = low;
    void *result = NULL;
    int status;
    while ((status = avxemu_maps_next(&reader, &mapping, 0)) == 1) {
        if (candidate < mapping.begin && size <= mapping.begin - candidate) {
            result = map_exact(candidate, size, PROT_READ | PROT_WRITE | PROT_EXEC);
            if (result) break;
        }
        if (mapping.end > candidate) candidate = mapping.end;
        if (candidate > high || size - 1 > high - candidate) break;
    }
    if (!result && status == 0 && candidate <= high && size - 1 <= high - candidate)
        result = map_exact(candidate, size, PROT_READ | PROT_WRITE | PROT_EXEC);
    close(reader.fd);
    return result;
}
