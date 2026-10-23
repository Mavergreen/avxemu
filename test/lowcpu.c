#define _GNU_SOURCE
#include <assert.h>
#include <cpuid.h>
#include <gnu/libc-version.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void invalid_lock_bsr(void);
extern void invalid_lock_bsf(void);
extern uint32_t lzcnt32(uint32_t);
extern uint32_t tzcnt32(uint32_t);
extern uint64_t bmi_shift(uint64_t, unsigned, uint64_t *);
extern uint64_t bmi_multiply(uint64_t, uint64_t, uint64_t *, uint64_t *);
extern void vector_add(const uint32_t *, const uint32_t *, uint32_t *);
extern void vector_fma(const float *, const float *, const float *, float *);
extern uint64_t count_flags(uint32_t, uint64_t *);

static int features(const char *variant) {
    unsigned a, b, c, d, lo, hi;
    __cpuid_count(1, 0, a, b, c, d);
    unsigned sse42=(c>>20)&1, avx=(c>>28)&1, osxsave=(c>>27)&1, fma=(c>>12)&1;
    assert(sse42 && avx && osxsave);
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    uint64_t xcr0=((uint64_t)hi<<32)|lo;
    __cpuid_count(7, 0, a, b, c, d);
    unsigned avx2=(b>>5)&1, bmi1=(b>>3)&1, bmi2=(b>>8)&1;
    __cpuid_count(0x80000001, 0, a, b, c, d);
    unsigned lzcnt=(c>>5)&1;
    printf("raw features: SSE4.2=%u AVX=%u OSXSAVE=%u XCR0=%llx AVX2=%u BMI1=%u BMI2=%u FMA=%u LZCNT=%u glibc=%s\n",
           sse42,avx,osxsave,(unsigned long long)xcr0,avx2,bmi1,bmi2,fma,lzcnt,gnu_get_libc_version());
    assert((xcr0&7)==7 && !avx2 && !bmi2 && !fma);
    assert(bmi1 == (unsigned)!strcmp(variant,"bmi1"));
    assert(lzcnt == (unsigned)!strcmp(variant,"lzcnt"));
    /* Confirm that advertised count features execute with their own semantics
     * before loading avxemu; independent feature models expose translator bugs. */
    if (lzcnt) { assert(lzcnt32(0)==32); assert(lzcnt32(1)==31); }
    if (bmi1) { assert(tzcnt32(0)==32); assert(tzcnt32(8)==3); }
    return 0;
}

static void check(void) {
    const uint32_t a[8]={1,2,3,4,0xffffffffu,100,0x80000000u,12345};
    const uint32_t b[8]={9,8,7,6,2,200,0x80000000u,54321};
    const uint32_t want[8]={10,10,10,10,1,300,0,66666};
    uint32_t out[8];
    vector_add(a,b,out);
    for (int i=0;i<8;i++) assert(out[i]==want[i]);
    const float x[8]={1,2,3,4,-1,-2,-3,-4};
    const float y[8]={2,4,8,16,2,4,8,16};
    const float z[8]={1,1,1,1,1,1,1,1};
    const float fwant[8]={3,9,25,65,-1,-7,-23,-63};
    float fout[8];
    vector_fma(x,y,z,fout);
    for (int i=0;i<8;i++) assert(fout[i]==fwant[i]);
    uint64_t flags, high;
    assert(bmi_shift(UINT64_C(0x123456789),7,&flags)==UINT64_C(0x91a2b3c480));
    assert((flags&0x8d5)==0x45);
    assert(bmi_multiply(UINT64_MAX,2,&high,&flags)==UINT64_MAX-1);
    assert(high==1 && (flags&0x8d5)==0x45);
    assert(lzcnt32(0)==32);
    assert(lzcnt32(1)==31);
    assert(tzcnt32(0)==32);
    assert(tzcnt32(8)==3);
    uint64_t zflags;
    flags=count_flags(0,&zflags);
    assert((flags&0x41)==1 && (zflags&0x41)==1);
    flags=count_flags(0x80000001u,&zflags);
    assert((flags&0x41)==0x40 && (zflags&0x41)==0x40);
}

static pthread_barrier_t start;
static void *worker(void *arg) {
    (void)arg;
    int rc=pthread_barrier_wait(&start);
    assert(rc==0 || rc==PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i=0;i<400;i++) check();
    return NULL;
}

int main(int argc, char **argv) {
    if (argc==3 && !strcmp(argv[1],"features")) return features(argv[2]);
    if (argc==2 && !strcmp(argv[1],"native-lock-bsr")) { invalid_lock_bsr(); return 1; }
    if (argc==2 && !strcmp(argv[1],"native-lock-bsf")) { invalid_lock_bsf(); return 1; }
    if (argc==2 && !strcmp(argv[1],"native")) {
        uint32_t a[8]={0}, out[8];
        vector_add(a,a,out);
        fputs("FAIL: native AVX2 unexpectedly executed\n",stderr);
        return 1;
    }
    if (argc==2 && !strcmp(argv[1],"emulated")) {
        pthread_t threads[4];
        check();
        assert(!pthread_barrier_init(&start,NULL,4));
        for (int i=0;i<4;i++) assert(!pthread_create(&threads[i],NULL,worker,NULL));
        for (int i=0;i<4;i++) assert(!pthread_join(threads[i],NULL));
        assert(!pthread_barrier_destroy(&start));
        puts("lowcpu: PASS (all vector lanes, FMA, BMI shift/multiply, flags, LZCNT/TZCNT; 4 threads x 400)");
        return 0;
    }
    fprintf(stderr,"usage: %s features {base|lzcnt|bmi1} | native | native-lock-bsr | native-lock-bsf | emulated\n",argv[0]);
    return 2;
}
