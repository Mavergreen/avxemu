#define _GNU_SOURCE
#include "platform/context.h"
#include "platform/linux/xstate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

static void put32(void *p, uint32_t n) { memcpy(p,&n,4); }
static void put64(void *p, uint64_t n) { memcpy(p,&n,8); }
static void fixture(uint8_t *bytes, size_t size) {
    memset(bytes,0,size);
    put32(bytes+464,0x46505853); put32(bytes+468,(uint32_t)size);
    put64(bytes+472,15); put32(bytes+480,(uint32_t)size-4);
    put64(bytes+512,15); put32(bytes+size-4,0x46505845);
}
static void rejects(uint8_t *bytes, size_t size) {
    avxemu_xstate view, before;
    memset(&view,0xa5,sizeof view); before=view;
    assert(!avxemu_xstate_view(bytes,size,&view));
    assert(!memcmp(&view,&before,sizeof view));
}
int main(void) {
    _Alignas(64) uint8_t bytes[1028], before[1028];
    avxemu_xstate view;
    fixture(bytes,836); put64(bytes+472,7); put64(bytes+512,7);
    assert(avxemu_xstate_view(bytes,836,&view));
    fixture(bytes,sizeof bytes);
    assert(avxemu_xstate_view(bytes,sizeof bytes,&view));
    assert(view.xmm==bytes+160 && view.ymmh==bytes+576 && view.header==bytes+512 && view.active==15);
    for (size_t size=0; size<sizeof bytes; size++) rejects(bytes,size);
    rejects(0,sizeof bytes);
    assert(!avxemu_xstate_view(bytes,sizeof bytes,0));
    static const struct { size_t offset; uint64_t value; int wide; } bad[] = {
        {464,0,0}, {464,0x46505852,0}, {468,0,0}, {468,832,0},
        {468,1027,0}, {468,UINT32_MAX,0}, {480,0,0}, {480,831,0},
        {480,1025,0}, {480,UINT32_MAX,0}, {472,3,1}, {472,5,1},
        {472,6,1}, {512,31,1}, {520,1ull<<63,1}, {520,7,1},
        {528,1,1}, {536,1,1}, {568,1,1}, {1024,0,0}
    };
    for (size_t i=0; i<sizeof bad/sizeof *bad; i++) {
        fixture(bytes,sizeof bytes);
        if (bad[i].wide) put64(bytes+bad[i].offset,bad[i].value);
        else put32(bytes+bad[i].offset,(uint32_t)bad[i].value);
        rejects(bytes,sizeof bytes);
    }
    fixture(bytes,sizeof bytes);
    put64(bytes+512,9); /* SSE and YMM are supported, but initially inactive. */
    memset(bytes+160,0xa5,256); memset(bytes+576,0x5a,256);
    memset(bytes+832,0x73,192); memcpy(before,bytes,sizeof bytes);
    ucontext_t uc; memset(&uc,0,sizeof uc); uc.uc_mcontext.fpregs=(void *)bytes;
    avxemu_regfile rf, saved;
    memset(&rf,0xa5,sizeof rf);
    assert(avxemu_context_read(&uc,&rf,1));
    for (int i=0; i<16; i++) for (int j=0; j<32; j++) assert(rf.ymm[i][j]==0);
    for (int i=0; i<16; i++) memset(rf.ymm[i],i+1,32);
    assert(avxemu_context_write(&uc,&rf,1));
    assert(avxemu_xstate_view(bytes,sizeof bytes,&view) && view.active==15);
    for (size_t i=0; i<sizeof bytes; i++)
        if (!(i>=160 && i<416) && !(i>=512 && i<520) && !(i>=576 && i<832)) assert(bytes[i]==before[i]);
    assert(avxemu_context_read(&uc,&saved,1) && !memcmp(saved.ymm,rf.ymm,sizeof rf.ymm));

    uc.uc_mcontext.fpregs=0;
    memset(&rf,0xa5,sizeof rf); saved=rf;
    assert(!avxemu_context_read(&uc,&rf,1) && !memcmp(&rf,&saved,sizeof rf));
    ucontext_t old=uc;
    assert(!avxemu_context_write(&uc,&rf,1) && !memcmp(&uc,&old,sizeof uc));
    assert(avxemu_context_read(&uc,&rf,0) && !memcmp(rf.ymm,saved.ymm,sizeof rf.ymm));
    assert(avxemu_context_write(&uc,&rf,0));

    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *guard=mmap(0,3*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(guard!=MAP_FAILED && !mprotect(guard+page,page,PROT_NONE));
    /* Every truncated prefix physically ends at an unreadable page, so a
     * premature metadata or trailer read fails even without a sanitizer. */
    fixture(bytes,sizeof bytes);
    for (size_t size=0; size<sizeof bytes; size++) {
        uint8_t *short_frame=guard+page-size;
        memcpy(short_frame,bytes,size);
        rejects(short_frame,size);
    }
    /* Readable trailer beyond a hole cannot validate a forged extent. */
    fixture(guard,832); put32(guard+468,(uint32_t)(2*page+4));
    put32(guard+480,(uint32_t)(2*page)); put32(guard+2*page,0x46505845);
    uc.uc_mcontext.fpregs=(void *)guard; old=uc; saved=rf;
    assert(!avxemu_context_read(&uc,&rf,1) && !memcmp(&rf,&saved,sizeof rf));
    assert(!avxemu_context_write(&uc,&rf,1) && !memcmp(&uc,&old,sizeof uc));
    /* Even metadata itself can straddle unreadable memory. */
    uc.uc_mcontext.fpregs=(void *)(guard+page-512);
    assert(!avxemu_context_read(&uc,&rf,1) && !memcmp(&rf,&saved,sizeof rf));
    assert(!munmap(guard,3*page));
    puts("xstate-contract: PASS (bounds, format, initial state, preservation, guarded frames)");
    return 0;
}
