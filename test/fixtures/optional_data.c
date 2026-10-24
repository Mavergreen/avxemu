#include <stdio.h>
#include <string.h>
extern unsigned char embedded_cpuid[], embedded_vector[];
extern void cpuid_dispatch(void (*)(void)), vector_dispatch(void (*)(void));
extern void real_cpuid(void);
static void return_target(void) {}
int main(void) {
    static const unsigned char cpuid[]={0x0f,0xa2,0xc3};
    static const unsigned char vector[]={0xc5,0xfd,0xfe,0xc1,0xc5,0xfd,0xfe,0xc1,0xc3};
    cpuid_dispatch(return_target); vector_dispatch(return_target); real_cpuid();
    int bad=memcmp(embedded_cpuid,cpuid,sizeof cpuid)||memcmp(embedded_vector,vector,sizeof vector);
    puts(bad?"optional data: FAIL":"optional data: PASS");
    return bad;
}
