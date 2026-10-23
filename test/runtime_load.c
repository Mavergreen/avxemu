#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#ifdef AVXEMU_LINKED
extern void avxemu_force_install(void);
#endif
__attribute__((noinline)) static unsigned count(unsigned value) {
    unsigned out;
    __asm__("lzcntl %1,%0" : "=r"(out) : "r"(value));
    return out;
}
int main(void) {
#ifdef AVXEMU_LINKED
    void (*install)(void) = avxemu_force_install;
#else
    void (*install)(void) = dlsym(RTLD_DEFAULT, "avxemu_force_install");
#endif
    if (!install) return 2;
    puts("avxemu loaded"); fflush(stdout);
    install();
    if (count(0) != 32 || count(1) != 31) return 3;
    puts("runtime-load: OK");
    return 0;
}
