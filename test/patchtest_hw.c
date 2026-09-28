/*
 * patchtest_hw.c — ground truth for patchtest.c: real lzcnt/tzcnt, compiled
 * with -mlzcnt on a host that has it. Called only in record mode.
 */
#include "patchtest_hw.h"

uint64_t patch_hw_lzcnt64(uint64_t x){ uint64_t r; __asm__("lzcntq %1,%0":"=r"(r):"r"(x)); return r; }
uint64_t patch_hw_tzcnt64(uint64_t x){ uint64_t r; __asm__("tzcntq %1,%0":"=r"(r):"r"(x)); return r; }
uint32_t patch_hw_lzcnt32(uint32_t x){ uint32_t r; __asm__("lzcntl %1,%0":"=r"(r):"r"(x)); return r; }
uint32_t patch_hw_tzcnt32(uint32_t x){ uint32_t r; __asm__("tzcntl %1,%0":"=r"(r):"r"(x)); return r; }
