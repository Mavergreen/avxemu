#ifndef AVXEMU_PLATFORM_MEMORY_H
#define AVXEMU_PLATFORM_MEMORY_H
#include <stddef.h>
#include <stdint.h>

enum { AVXEMU_PROT_R = 1, AVXEMU_PROT_W = 2, AVXEMU_PROT_X = 4 };
typedef struct { uintptr_t begin, end; unsigned prot; } avxemu_mapping;

/* Cache the page size during initialization, before installing handlers.
 * After that, these operations use bounded storage and are handler-safe. */
size_t avxemu_page_size(void);
/* 1: mapped (including protection zero), 0: verified gap, -1: unknown/error.
 * Failed queries leave mapping unchanged. */
int avxemu_memory_query(uintptr_t address, avxemu_mapping *mapping);
/* Return the readable prefix copied, without faulting on inaccessible input. */
size_t avxemu_memory_read(void *dst, uintptr_t src, size_t size);
int avxemu_memory_protect(uintptr_t address, size_t size, unsigned prot);
/* Accept a nonempty span in one mapping. Add write while retaining execute;
 * save the changed page span and original permissions, only on success.
 * Callers serialize patching and split spans at mapping boundaries. */
int avxemu_memory_patch_begin(uintptr_t address, size_t size,
                              avxemu_mapping *saved);
int avxemu_memory_patch_end(const avxemu_mapping *saved);
/* Return an RWX allocation wholly within rel32 reach, or NULL on failure. */
void *avxemu_memory_alloc_near(uintptr_t site, size_t size);
/* Map zero-filled RW pages at this exact address without replacing mappings. */
int avxemu_memory_map_gap(uintptr_t address, size_t size);
#endif
