#ifndef AVXEMU_BMI_HW_H
#define AVXEMU_BMI_HW_H
#include <stdint.h>

uint64_t bmi_hw_andn(uint64_t a, uint64_t b, uint64_t *f);
uint64_t bmi_hw_blsi(uint64_t a, uint64_t *f);
uint64_t bmi_hw_blsr(uint64_t a, uint64_t *f);
uint64_t bmi_hw_blsmsk(uint64_t a, uint64_t *f);
uint64_t bmi_hw_bzhi(uint64_t a, uint64_t i, uint64_t *f);
uint64_t bmi_hw_bextr(uint64_t a, uint64_t c, uint64_t *f);
uint64_t bmi_hw_tzcnt(uint64_t a, uint64_t *f);
uint64_t bmi_hw_lzcnt(uint64_t a, uint64_t *f);
uint64_t bmi_hw_rorx13(uint64_t a);
uint64_t bmi_hw_shlx(uint64_t a, uint64_t c);
uint64_t bmi_hw_shrx(uint64_t a, uint64_t c);
uint64_t bmi_hw_sarx(uint64_t a, uint64_t c);
uint64_t bmi_hw_movbe(uint64_t a);
uint64_t bmi_hw_mulx(uint64_t a, uint64_t b, uint64_t *hi);
uint64_t bmi_hw_pdep(uint64_t a, uint64_t b);
uint64_t bmi_hw_pext(uint64_t a, uint64_t b);

#endif
