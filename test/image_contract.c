#include "image.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    uint8_t bytes[96];
    avxemu_code_range ranges[] = {
        {bytes, 0x1000, 32, 40, AVXEMU_PROT_R | AVXEMU_PROT_X},
        {bytes + 64, 0x2000, 32, 32, AVXEMU_PROT_R | AVXEMU_PROT_X}
    };
    avxemu_function funcs[] = {{0, 4, 12}, {0, 16, 24}, {1, 0, 32}};
    avxemu_image image = {ranges, 2, funcs, 3, 0, 0, 0, AVXEMU_SCAN_STRICT};
    uint32_t r = 99; size_t off = 99;
    assert(avxemu_image_find(&image, (uintptr_t)bytes + 31, 1, &r, &off));
    assert(r == 0 && off == 31);
    assert(!avxemu_image_find(&image, (uintptr_t)bytes + 31, 2, &r, &off));
    assert(r == 0 && off == 31); /* failure leaves outputs unchanged */
    assert(!avxemu_image_find(&image, (uintptr_t)bytes + 32, 1, &r, &off));
    assert(!avxemu_image_find(&image, (uintptr_t)bytes + 48, 1, 0, 0));
    assert(avxemu_image_find(&image, (uintptr_t)bytes + 64, 32, &r, &off));
    assert(r == 1 && off == 0);
    assert(!avxemu_image_find(&image, UINTPTR_MAX - 1, 8, &r, &off));
    assert(!avxemu_image_find(&image, (uintptr_t)bytes, SIZE_MAX, 0, 0));
    assert(avxemu_image_function(&image, (uintptr_t)bytes + 4) == &funcs[0]);
    assert(avxemu_image_function(&image, (uintptr_t)bytes + 11) == &funcs[0]);
    assert(!avxemu_image_function(&image, (uintptr_t)bytes + 12));
    assert(!avxemu_image_function(&image, (uintptr_t)bytes + 13));
    assert(!avxemu_image_function(&image, (uintptr_t)bytes));
    assert(avxemu_image_function(&image, (uintptr_t)bytes + 95) == &funcs[2]);
    assert(!avxemu_image_function(&image, (uintptr_t)bytes + 96));
    assert(!avxemu_image_find(0, 0, 1, 0, 0));
    image.nranges = 0;
    assert(!avxemu_image_find(&image, (uintptr_t)bytes, 1, 0, 0));
    image.nranges = (size_t)UINT32_MAX + 1;
    assert(!avxemu_image_find(&image, (uintptr_t)bytes, 1, 0, 0));
    image.nranges = 2; image.nfunctions = (size_t)UINT32_MAX + 1;
    assert(!avxemu_image_function(&image, (uintptr_t)bytes + 4));
    image.nfunctions = 3;
    ranges[1].base = (uint8_t *)(UINTPTR_MAX - 8); ranges[1].size = 32;
    assert(!avxemu_image_find(&image, UINTPTR_MAX - 4, 1, 0, 0));
    ranges[1].base = bytes + 64; ranges[1].readable = 4;
    assert(!avxemu_image_find(&image, (uintptr_t)bytes + 64, 1, 0, 0));
    funcs[0].end = 40;
    assert(!avxemu_image_function(&image, (uintptr_t)bytes + 4));
    funcs[0].end=12;
    avxemu_data_span data[]={{0,12,16,0},{1,8,16,0}};
    image.data=data; image.ndata=2;
    assert(avxemu_image_data_valid(&image));
    assert(avxemu_image_data_overlap(&image,0,10,3));
    assert(!avxemu_image_data_overlap(&image,0,10,2));
    assert(!avxemu_image_function(&image,(uintptr_t)bytes+72));
    data[1]=(avxemu_data_span){0,15,18,0}; assert(!avxemu_image_data_valid(&image));
    data[1]=(avxemu_data_span){2,0,8,0}; assert(!avxemu_image_data_valid(&image));
    data[1]=(avxemu_data_span){1,8,40,0}; assert(!avxemu_image_data_valid(&image));
    data[1]=(avxemu_data_span){1,8,8,0}; assert(!avxemu_image_data_valid(&image));
    data[1]=(avxemu_data_span){1,8,16,0};
    image.strategy=(avxemu_scan_strategy)2; assert(!avxemu_image_data_valid(&image));
    image.strategy=AVXEMU_SCAN_STRICT; assert(avxemu_image_data_valid(&image));
    data[1].begin=0; assert(!avxemu_image_data_valid(&image)); /* function entry is data */
    puts("image-contract: OK");
    return 0;
}
