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
/* Verified non-instruction bytes inside executable ranges. */
enum { AVXEMU_DATA_OPAQUE=0, AVXEMU_DATA_JUMP32=1 };
typedef struct {
    uint32_t range;
    size_t begin, end;
    unsigned kind;
} avxemu_data_span;
typedef enum { AVXEMU_SCAN_STRICT=0, AVXEMU_SCAN_LEGACY=1 } avxemu_scan_strategy;
typedef struct {
    const avxemu_code_range *ranges;
    size_t nranges;
    const avxemu_function *functions;
    size_t nfunctions;
    const char *path;
    const avxemu_data_span *data;
    size_t ndata;
    avxemu_scan_strategy strategy;
} avxemu_image;
/* Data spans are sorted, disjoint, nonempty and range-bounded. Invalid metadata
 * fails validation; overlap queries fail closed. No allocation in queries. */
int avxemu_image_data_valid(const avxemu_image *image);
int avxemu_image_data_overlap(const avxemu_image *image, uint32_t range, size_t begin, size_t size);
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
