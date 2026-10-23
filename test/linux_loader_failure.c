#define _GNU_SOURCE
#include "platform/loader.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__wrap_dlvsym(void *handle, const char *name, const char *version) {
    (void)handle;
    assert(!strcmp(name,"sigaction") && !strcmp(version,"GLIBC_2.2.5"));
    const char *mode=getenv("AVXEMU_RESOLVE_FAILURE");
    if (mode && !strcmp(mode,"self")) return (void *)sigaction;
    if (mode && !strcmp(mode,"recursive")) assert(!avxemu_loader_resolve());
    return 0;
}
int main(void) {
    struct sigaction action, old, before;
    memset(&action,0,sizeof action); sigemptyset(&action.sa_mask);
    action.sa_handler=SIG_IGN;
    memset(&old,0xa5,sizeof old); before=old;
    assert(!avxemu_loader_resolve() && !avxemu_loader_init());
    errno=0;
    assert(avxemu_real_sigaction(SIGILL,&action,&old)==-1 && errno==ENOSYS);
    assert(!memcmp(&old,&before,sizeof old));
    errno=0;
    assert(sigaction(SIGILL,&action,&old)==-1 && errno==ENOSYS);
    assert(!memcmp(&old,&before,sizeof old));
    errno=0;
    assert(signal(SIGILL,SIG_IGN)==SIG_ERR && errno==ENOSYS);
    puts("linux-loader-failure: PASS (resolution failure is explicit and transactional)");
    return 0;
}
