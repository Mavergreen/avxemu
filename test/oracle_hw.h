#ifndef AVXEMU_ORACLE_HW_H
#define AVXEMU_ORACLE_HW_H
#include "vexops.h"
#include <stdint.h>

int    oracle_hw_simple(vex_op op, const ymm256 *a, const ymm256 *b, const ymm256 *c, ymm256 *out, uint64_t *gpr);
void   oracle_hw_imm(int idx, uint8_t imm, const ymm256 *a, const ymm256 *b, ymm256 *out);
double oracle_hw_fma_d(int variant, double m1, double m2, double ad);
float  oracle_hw_fma_f(int variant, float m1, float m2, float ad);

#endif
