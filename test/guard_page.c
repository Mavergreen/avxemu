#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <unistd.h>
extern void guard_operand(const void *);
int main(int argc, char **argv){
    (void)argv;
    if (!dlsym(RTLD_DEFAULT, "avxemu_force_install")) return 2;
    puts("avxemu loaded"); fflush(stdout);
    uint8_t *base = mmap(0,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    mprotect(base+0x1000,0x1000,PROT_NONE);           /* guard page (mapped, no access) */
    if (argc > 1) {
        int *test_ud2 = dlsym(RTLD_DEFAULT, "avxemu_test_ud2");
        if (!test_ud2) return 2;
        *test_ud2 = 1;
        guard_operand(base + 0x1000 - 16);
        puts("MASKED emulated cross-page operand (BAD)"); return 1;
    }
    volatile uint64_t v; __asm__ volatile("movq (%1),%0":"=r"(v):"r"(base+0x1000+12));
    printf("MASKED guard page (BAD): v=0x%llx\n",(unsigned long long)v); return 0;
}
