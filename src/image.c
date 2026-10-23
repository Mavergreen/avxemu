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
        !image->functions || image->nfunctions > UINT32_MAX ||
        avxemu_image_data_overlap(image,range,off,1)) return 0;
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

int avxemu_image_data_valid(const avxemu_image *image) {
    if(!image || image->nranges>UINT32_MAX || (image->nranges && !image->ranges) || (image->strategy!=AVXEMU_SCAN_STRICT && image->strategy!=AVXEMU_SCAN_LEGACY) || image->ndata>UINT32_MAX || (image->ndata && !image->data) ||
        image->nfunctions>UINT32_MAX || (image->nfunctions && !image->functions)) return 0;
    for(size_t i=0;i<image->ndata;i++) {
        const avxemu_data_span *d=&image->data[i];
        if(!image->ranges || d->range>=image->nranges || d->begin>=d->end ||
            d->end>image->ranges[d->range].size || d->kind>AVXEMU_DATA_JUMP32 ||
            (d->kind==AVXEMU_DATA_JUMP32 && (d->end-d->begin)%4)) return 0;
        if(i && (d->range<image->data[i-1].range ||
            (d->range==image->data[i-1].range && d->begin<image->data[i-1].end))) return 0;
    }
    for(size_t i=0;i<image->nfunctions;i++) {
        const avxemu_function *f=&image->functions[i];
        if(avxemu_image_data_overlap(image,f->range,f->begin,1)) return 0;
    }
    return 1;
}
int avxemu_image_data_overlap(const avxemu_image *image, uint32_t range, size_t begin, size_t size) {
    if(!image || !image->ranges || image->ndata>UINT32_MAX || range>=image->nranges || begin>image->ranges[range].size ||
        size>image->ranges[range].size-begin || (image->ndata && !image->data)) return 1;
    if(!size) return 0;
    size_t lo=0,hi=image->ndata;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        const avxemu_data_span *d=&image->data[mid];
        if(d->range<range || (d->range==range && d->end<=begin)) lo=mid+1;
        else hi=mid;
    }
    return lo<image->ndata && image->data[lo].range==range && image->data[lo].begin<begin+size;
}
