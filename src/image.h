/* Immutable executable metadata, prepared before installing signal handlers. */
#ifndef AVXEMU_IMAGE_H
#define AVXEMU_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#include "platform/memory.h"

typedef struct {
    uint8_t *base;
    uint64_t vaddr;
    size_t size, readable;
    unsigned prot;
} avxemu_code_range;
typedef struct {
    uint32_t range;
    size_t begin, end;
} avxemu_function;
typedef struct {
    const avxemu_code_range *ranges;
    size_t nranges;
    const avxemu_function *functions;
    size_t nfunctions;
    const char *path;
} avxemu_image;
/* Initialization may allocate; failure publishes no image. */
int avxemu_image_init(void);
const avxemu_image *avxemu_image_main(void);
/* Allocation-free, handler-safe queries. Ranges are ordered by vaddr (and
 * loaded address), disjoint and nonempty; functions by (range, begin).
 * Bounds are half-open. Failure leaves output arguments unchanged. */
int avxemu_image_find(const avxemu_image *image, uintptr_t address,
                      size_t size, uint32_t *range, size_t *offset);
const avxemu_function *avxemu_image_function(const avxemu_image *image,
                                             uintptr_t address);
#endif
