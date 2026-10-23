#ifndef AVXEMU_CPUID_H
#define AVXEMU_CPUID_H
#include "regfile.h"
#include "image.h"
void avxemu_cpuid_raw(uint32_t leaf, uint32_t sub, uint32_t result[4]);
int avxemu_get_cpuid(uint32_t leaf, uint32_t sub, uint32_t result[4]);
int avxemu_cpu_baseline(void);
void avxemu_cpuid_configure(void);
void avxemu_patch_cpuid(void);
/* Explicit-image entry point also exercises discovery independently of startup. */
long avxemu_cpuid_patch_image(const avxemu_image *image, int recursive);
int avxemu_cpuid_trap(uintptr_t address);
int avxemu_cpuid_handle(avxemu_regfile *rf);
/* Startup owns this plan from prepare through apply/discard. Legacy preparation
 * may use a locally validated replay; strict preparation always rediscovers.
 * Image metadata and
 * instruction bytes must remain immutable except for owned CPUID trap edits.
 * Apply validates all original prefix bytes and consumes the plan on every path. */
typedef struct avxemu_zcnt_plan avxemu_zcnt_plan;
avxemu_zcnt_plan *avxemu_zcnt_prepare(const avxemu_image *image, unsigned required);
void avxemu_zcnt_discard(avxemu_zcnt_plan *plan);
long avxemu_zcnt_apply(avxemu_zcnt_plan *plan);
int avxemu_zcnt_preflight(const avxemu_image *image, unsigned required);
unsigned avxemu_zcnt_required(uint32_t leaf7_ebx, uint32_t ext1_ecx, int force);
#endif
