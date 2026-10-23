#include "image.h"

int avxemu_image_find(const avxemu_image *image, uintptr_t address,
                      size_t size, uint32_t *range, size_t *offset) {
    if (!image || !image->ranges || !image->nranges || image->nranges > UINT32_MAX)
        return 0;
    size_t lo = 0, hi = image->nranges;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uintptr_t)image->ranges[mid].base <= address) lo = mid + 1;
        else hi = mid;
    }
    if (!lo) return 0;
    const avxemu_code_range *r = &image->ranges[lo - 1];
    uintptr_t base = (uintptr_t)r->base;
    if (!r->size || r->readable < r->size || r->readable > UINTPTR_MAX - base ||
        r->size > UINT64_MAX - r->vaddr) return 0;
    size_t off = (size_t)(address - base);
    if (off >= r->size || size > r->size - off) return 0;
    if (range) *range = (uint32_t)(lo - 1);
    if (offset) *offset = off;
    return 1;
}

const avxemu_function *avxemu_image_function(const avxemu_image *image,
                                             uintptr_t address) {
    uint32_t range; size_t off;
    if (!avxemu_image_find(image, address, 1, &range, &off) ||
        !image->functions || image->nfunctions > UINT32_MAX) return 0;
    size_t lo = 0, hi = image->nfunctions;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const avxemu_function *f = &image->functions[mid];
        if (f->range < range || (f->range == range && f->begin <= off)) lo = mid + 1;
        else hi = mid;
    }
    if (!lo) return 0;
    const avxemu_function *f = &image->functions[lo - 1];
    if (f->range != range || f->begin >= f->end ||
        f->end > image->ranges[range].size || off >= f->end) return 0;
    return f;
}
