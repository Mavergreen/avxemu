#define _DARWIN_C_SOURCE
#include <limits.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <unistd.h>
#include "platform/memory.h"

static size_t page_size;

size_t avxemu_page_size(void) {
    if (!page_size) {
        long size = sysconf(_SC_PAGESIZE);
        if (size > 0) page_size = (size_t)size;
    }
    return page_size;
}

static vm_prot_t native_prot(unsigned prot) {
    return ((prot & AVXEMU_PROT_R) ? VM_PROT_READ : 0) |
           ((prot & AVXEMU_PROT_W) ? VM_PROT_WRITE : 0) |
           ((prot & AVXEMU_PROT_X) ? VM_PROT_EXECUTE : 0);
}

int avxemu_memory_query(uintptr_t address, avxemu_mapping *mapping) {
    if (!mapping) return -1;
    mach_vm_address_t begin = address;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(mach_task_self(), &begin, &size,
                                      VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    /* No region at or above the address is a verified terminal gap. Other
     * errors cannot authorize mapping or patching. */
    if (kr == KERN_INVALID_ADDRESS) return 0;
    if (kr != KERN_SUCCESS || count < VM_REGION_BASIC_INFO_COUNT_64) return -1;
    if (begin > address) return 0;
    if (!size || size > UINTPTR_MAX - begin || address - begin >= size) return -1;
    avxemu_mapping result = {(uintptr_t)begin, (uintptr_t)(begin + size), 0};
    if (info.protection & VM_PROT_READ) result.prot |= AVXEMU_PROT_R;
    if (info.protection & VM_PROT_WRITE) result.prot |= AVXEMU_PROT_W;
    if (info.protection & VM_PROT_EXECUTE) result.prot |= AVXEMU_PROT_X;
    *mapping = result;
    return 1;
}

size_t avxemu_memory_read(void *dst, uintptr_t src, size_t size) {
    size_t page = avxemu_page_size(), done = 0;
    if (!page || size > UINTPTR_MAX - src) return 0;
    while (done < size) {
        size_t n = page - (src + done) % page;
        if (n > size - done) n = size - done;
        mach_vm_size_t got = 0;
        kern_return_t kr = mach_vm_read_overwrite(mach_task_self(), src + done, n,
                               (mach_vm_address_t)(uintptr_t)((char *)dst + done), &got);
        if (kr != KERN_SUCCESS || got > n) break;
        done += (size_t)got;
        if (got != n) break;
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

int avxemu_memory_protect(uintptr_t address, size_t size, unsigned prot) {
    uintptr_t begin, end;
    if (prot & ~(AVXEMU_PROT_R | AVXEMU_PROT_W | AVXEMU_PROT_X)) return 0;
    if (!page_span(address, size, &begin, &end)) return 0;
    return mach_vm_protect(mach_task_self(), begin, end - begin, FALSE,
                          native_prot(prot) | ((prot & AVXEMU_PROT_W) ? VM_PROT_COPY : 0)) == KERN_SUCCESS;
}

int avxemu_memory_patch_begin(uintptr_t address, size_t size, avxemu_mapping *saved) {
    avxemu_mapping mapping;
    uintptr_t begin, end;
    if (!saved || !page_span(address, size, &begin, &end)) return 0;
    if (avxemu_memory_query(address, &mapping) != 1 ||
        begin < mapping.begin || end > mapping.end) return 0;
    if (!avxemu_memory_protect(begin, end - begin, mapping.prot | AVXEMU_PROT_W)) return 0;
    mapping.begin = begin;
    mapping.end = end;
    *saved = mapping;
    return 1;
}

int avxemu_memory_patch_end(const avxemu_mapping *saved) {
    size_t page = avxemu_page_size();
    if (!saved || !page || saved->begin >= saved->end ||
        saved->begin % page || saved->end % page) return 0;
    return avxemu_memory_protect(saved->begin, saved->end - saved->begin, saved->prot);
}

void *avxemu_memory_alloc_near(uintptr_t site, size_t size) {
    uintptr_t begin, end;
    if (!page_span(0, size, &begin, &end)) return NULL;
    size = end;
    void *p = mmap((void *)site, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return NULL;
    uintptr_t address = (uintptr_t)p;
    /* Leave room for the rel32 instruction at either end of a branch. */
    const uintptr_t reach = INT32_MAX - 5;
    uintptr_t low = site > reach ? site - reach : 0;
    uintptr_t high = site <= UINTPTR_MAX - reach ? site + reach : UINTPTR_MAX;
    if (address < low || address > high || size - 1 > high - address) {
        munmap(p, size);
        return NULL;
    }
    return p;
}

int avxemu_memory_map_gap(uintptr_t address, size_t size) {
    size_t page = avxemu_page_size();
    if (!page || !size || address % page || size % page || size > UINTPTR_MAX - address) return 0;
    avxemu_mapping mapping;
    if (avxemu_memory_query(address, &mapping) != 0) return 0;
    mach_vm_address_t target = address;
    /* Fixed Mach allocation fails on overlaps; VM_FLAGS_OVERWRITE is never
     * used, so a concurrent mapping cannot be replaced after the query. */
    return mach_vm_allocate(mach_task_self(), &target, size, VM_FLAGS_FIXED) == KERN_SUCCESS;
}
