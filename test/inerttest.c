/*
 * inerttest.c — on a CPU that already has everything avxemu emulates, a LINKED
 * avxemu must do nothing at all: leave SIGILL alone and leave __text alone.
 *
 * Otherwise "just always link it" is not free on an AVX2 Mac: the cpuid pass
 * decodes all of __text at every launch (it advertises AVX2 whatever the CPU),
 * every cpuid traps from then on, and the app's SIGILL handler becomes ours to
 * chain to.
 *
 * On a capable CPU the plain run checks the real early return. On one that lacks
 * the features, AVXEMU_ASSUME_CAPABLE=1 takes the same early return, and the
 * plain run must find avxemu installed -- the negative control, proving the
 * checks can fail (they caught the constructor attribute landing on the wrong
 * function).
 *
 *   inerttest                           expects: inert if capable, else installed
 *   AVXEMU_ASSUME_CAPABLE=1 inerttest   expects: inert
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

/* A cpuid in our own __text, where avxemu's cpuid pass would rewrite it. */
__asm__(".text\n.globl _probe_cpuid\n_probe_cpuid:\n cpuid\n ret\n");
extern const uint8_t probe_cpuid[];

int main(void) {
    int assume = getenv("AVXEMU_ASSUME_CAPABLE") != 0;
    /* avxemu's own predicate, all seven features, not AVX2 alone: a CPU (or a translator)
     * can report AVX2 without, say, MOVBE, and there avxemu installs. */
    int real = cpu_avxemu_capable();
    struct sigaction cur;
    sigaction(SIGILL, 0, &cur);
    int handler_ours = cur.sa_handler != SIG_DFL;
    int text_patched = !(probe_cpuid[0] == 0x0F && probe_cpuid[1] == 0xA2);

    const char *mode = assume ? "AVXEMU_ASSUME_CAPABLE=1" : real ? "capable CPU" : "lacking CPU";
    int want = !(assume || real);   /* installed exactly when the CPU needs us */
    if (handler_ours == want && text_patched == want) {
        printf("PASS inerttest (%s): SIGILL %s, __text %s\n", mode,
               want ? "taken" : "untouched", want ? "patched" : "untouched");
        return 0;
    }
    printf("FAIL inerttest (%s): SIGILL handler %s, own cpuid %02x %02x\n", mode,
           handler_ours ? "installed" : "default", probe_cpuid[0], probe_cpuid[1]);
    return 1;
}
