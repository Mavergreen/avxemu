/*
 * bmi_hw.c — ground truth for bmi_oracle.c: real BMI1/BMI2/LZCNT/MOVBE, compiled
 * with -mbmi -mbmi2 -mlzcnt -mmovbe on a Haswell+ host. Called only in record mode.
 */
#include "bmi_hw.h"
#include <immintrin.h>
#include <x86intrin.h>

uint64_t bmi_hw_andn(uint64_t a,uint64_t b,uint64_t*f){uint64_t r,ff;__asm__ volatile("andnq %3,%2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a),"r"(b):"cc");*f=ff;return r;}
uint64_t bmi_hw_blsi(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("blsiq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
uint64_t bmi_hw_blsr(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("blsrq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
uint64_t bmi_hw_blsmsk(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("blsmskq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
uint64_t bmi_hw_bzhi(uint64_t a,uint64_t i,uint64_t*f){uint64_t r,ff;__asm__ volatile("bzhiq %3,%2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a),"r"(i):"cc");*f=ff;return r;}
uint64_t bmi_hw_bextr(uint64_t a,uint64_t c,uint64_t*f){uint64_t r,ff;__asm__ volatile("bextrq %3,%2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a),"r"(c):"cc");*f=ff;return r;}
uint64_t bmi_hw_tzcnt(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("tzcntq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
uint64_t bmi_hw_lzcnt(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("lzcntq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
uint64_t bmi_hw_rorx13(uint64_t a){uint64_t r;__asm__ volatile("rorxq %2,%1,%0":"=r"(r):"r"(a),"i"(13));return r;}
uint64_t bmi_hw_shlx(uint64_t a,uint64_t c){uint64_t r;__asm__ volatile("shlxq %2,%1,%0":"=r"(r):"r"(a),"r"(c));return r;}
uint64_t bmi_hw_shrx(uint64_t a,uint64_t c){uint64_t r;__asm__ volatile("shrxq %2,%1,%0":"=r"(r):"r"(a),"r"(c));return r;}
uint64_t bmi_hw_sarx(uint64_t a,uint64_t c){uint64_t r;__asm__ volatile("sarxq %2,%1,%0":"=r"(r):"r"(a),"r"(c));return r;}
uint64_t bmi_hw_movbe(uint64_t a){return __builtin_bswap64(a);}

uint64_t bmi_hw_mulx(uint64_t a, uint64_t b, uint64_t *hi) {
    unsigned long long h; uint64_t lo = _mulx_u64(a, b, &h); *hi = h; return lo;
}
uint64_t bmi_hw_pdep(uint64_t a, uint64_t b) { return _pdep_u64(a, b); }
uint64_t bmi_hw_pext(uint64_t a, uint64_t b) { return _pext_u64(a, b); }
