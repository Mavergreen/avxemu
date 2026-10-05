/*
 * linkhook.c — avxemu must keep SIGILL even when it is LINKED, not inserted.
 *
 * THE bug this pins: 10.9's dyld registers __DATA,__interpose only for
 * DYLD_INSERT_LIBRARIES images. Baked into a binary as a plain LC_LOAD_DYLIB,
 * avxemu's sigaction()/signal() overrides are ignored, so a runtime that
 * installs its own SIGILL handler (Bun does, during startup) takes the signal
 * the emulator lives on and every emulated instruction turns into an "illegal
 * instruction" crash. handler.c answers that by rebinding the sigaction/signal
 * symbol pointers itself.
 *
 * Build this LINKED against the dylib — that is the whole point; running it
 * under DYLD_INSERT_LIBRARIES would test dyld's interposition instead of ours.
 *
 * Host-agnostic: it never executes an AVX2 instruction. On a CPU that has
 * everything avxemu stays inert (inerttest), so its ctests run this with
 * AVXEMU_FORCEPATCH=1, which keeps it installed; "did our registration replace
 * it?" is then a meaningful question on an AVX2 oracle box too. AVXEMU_NO_REBIND=1 is the
 * negative control — with the rebind off, the registration MUST get through.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>

extern int avxemu_rebind_count;

static void my_handler(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)si; (void)uc;
}

int main(void) {
    int expect_kept = (getenv("AVXEMU_NO_REBIND") == 0);
    const char *mode = expect_kept ? "rebind on" : "AVXEMU_NO_REBIND=1";

    struct sigaction mine, cur;
    memset(&mine, 0, sizeof mine);
    mine.sa_sigaction = my_handler;
    mine.sa_flags = SA_SIGINFO;
    sigemptyset(&mine.sa_mask);

    if (sigaction(SIGILL, &mine, 0) != 0) {
        printf("FAIL linkhook: sigaction(SIGILL) failed\n");
        return 1;
    }
    memset(&cur, 0, sizeof cur);
    if (sigaction(SIGILL, 0, &cur) != 0) {
        printf("FAIL linkhook: sigaction(SIGILL, NULL, &cur) failed\n");
        return 1;
    }

    /* Did our registration actually land, or did avxemu keep the handler and
     * merely record ours as its chain target? */
    int stolen = (cur.sa_sigaction == my_handler);

    if (expect_kept && stolen) {
        printf("FAIL linkhook (%s): the app's SIGILL handler replaced avxemu's "
               "— emulated instructions would crash\n", mode);
        return 1;
    }
    if (!expect_kept && !stolen) {
        /* Newer dyld honours __DATA,__interpose for linked images too, so there
         * is nothing for the rebind to do and nothing for this control to show.
         * That is the case iff our own sigaction resolves into avxemu although
         * the rebind rewrote nothing. Anything else is the control failing.
         * Asked of dyld: comparing &sigaction with &avxemu_sigaction in C is
         * folded to false at compile time (distinct functions, distinct
         * addresses), even at -O0. */
        Dl_info di;
        if (avxemu_rebind_count == 0 && dladdr((void *)&sigaction, &di) && di.dli_sname &&
            !strcmp(di.dli_sname, "avxemu_sigaction")) {
            printf("PASS linkhook (%-18s): not applicable -- this dyld interposes linked "
                   "images itself, so avxemu keeps SIGILL without the rebind\n", mode);
            return 0;
        }
        printf("FAIL linkhook (%s): rebind was disabled, yet the app's handler "
               "still did not take effect — the control case is broken\n", mode);
        return 1;
    }
    printf("PASS linkhook (%-18s): SIGILL %s\n", mode,
           expect_kept ? "still owned by avxemu" : "taken by the app, as expected");
    return 0;
}
