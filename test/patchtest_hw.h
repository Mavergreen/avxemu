#ifndef AVXEMU_PATCHTEST_HW_H
#define AVXEMU_PATCHTEST_HW_H
#include <stdint.h>

uint64_t patch_hw_lzcnt64(uint64_t x);
uint64_t patch_hw_tzcnt64(uint64_t x);
uint32_t patch_hw_lzcnt32(uint32_t x);
uint32_t patch_hw_tzcnt32(uint32_t x);

#endif
