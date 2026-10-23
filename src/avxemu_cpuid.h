#ifndef AVXEMU_CPUID_H
#define AVXEMU_CPUID_H
#include "regfile.h"
#include "image.h"
void avxemu_cpuid_raw(uint32_t leaf, uint32_t sub, uint32_t result[4]);
int avxemu_get_cpuid(uint32_t leaf, uint32_t sub, uint32_t result[4]);
void avxemu_cpuid_configure(void);
void avxemu_patch_cpuid(void);
/* Explicit-image entry point also exercises discovery independently of startup. */
long avxemu_cpuid_patch_image(const avxemu_image *image, int recursive);
int avxemu_cpuid_trap(uintptr_t address);
int avxemu_cpuid_handle(avxemu_regfile *rf);
unsigned avxemu_zcnt_required(uint32_t leaf7_ebx, uint32_t ext1_ecx, int force);
#endif
