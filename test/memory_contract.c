#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "platform/memory.h"

/* Removing restoration, treating PROT_NONE as a gap, or allowing exact maps
 * to overwrite pages must fail these checks against real kernel mappings. */
static void expect_fault(volatile unsigned char *p, int writing) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        struct rlimit no_core = {0, 0};
        (void)setrlimit(RLIMIT_CORE, &no_core);
        (void)signal(SIGSEGV, SIG_DFL);
        (void)signal(SIGBUS, SIG_DFL);
        if (writing) *p = 0x42;
        else { volatile unsigned char value = *p; (void)value; }
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
}

static void patch_permissions(unsigned char *p, size_t page, unsigned prot) {
    avxemu_mapping before, saved, after, neighbor;
    assert(avxemu_memory_protect((uintptr_t)p, 3 * page, prot));
    assert(avxemu_memory_query((uintptr_t)p, &before) == 1);
    assert(before.prot == prot);
    assert(avxemu_memory_patch_begin((uintptr_t)p + page + 7, 1, &saved));
    assert(saved.begin == (uintptr_t)p + page);
    assert(saved.end == (uintptr_t)p + 2 * page);
    assert(saved.prot == prot);
    assert(avxemu_memory_query((uintptr_t)p + page, &after) == 1);
    assert(after.prot == (prot | AVXEMU_PROT_W));
    assert(avxemu_memory_query((uintptr_t)p, &neighbor) == 1);
    assert(neighbor.prot == prot);
    assert(avxemu_memory_query((uintptr_t)p + 2 * page, &neighbor) == 1);
    assert(neighbor.prot == prot);
    p[page + 7] = 0x90;
    assert(avxemu_memory_patch_end(&saved));
    assert(avxemu_memory_query((uintptr_t)p + page, &after) == 1);
    assert(after.prot == before.prot);
    assert(p[page + 7] == 0x90);
    if (!(prot & AVXEMU_PROT_W)) expect_fault(p + page + 7, 1);
}

int main(void) {
    size_t page = avxemu_page_size();
    assert(page > 0 && page == (size_t)sysconf(_SC_PAGESIZE));
    unsigned char *p = mmap(NULL, 8 * page, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(p != MAP_FAILED);
    memset(p, 0x5a, 8 * page);
    avxemu_mapping mapping;
    assert(avxemu_memory_query((uintptr_t)p, &mapping) == 1);
    assert(mapping.begin <= (uintptr_t)p && mapping.end >= (uintptr_t)p + 8 * page);
    assert(mapping.prot == (AVXEMU_PROT_R | AVXEMU_PROT_W));
    patch_permissions(p, page, AVXEMU_PROT_R);
    patch_permissions(p, page, AVXEMU_PROT_R | AVXEMU_PROT_X);
    patch_permissions(p, page, AVXEMU_PROT_R | AVXEMU_PROT_W | AVXEMU_PROT_X);

    unsigned char data[32];
    assert(avxemu_memory_read(data, (uintptr_t)p + 4 * page, sizeof data) == sizeof data);
    for (size_t i = 0; i < sizeof data; i++) assert(data[i] == 0x5a);
    assert(avxemu_memory_read(data, UINTPTR_MAX, sizeof data) == 0);
    assert(avxemu_memory_read(data, (uintptr_t)p, 0) == 0);

    unsigned char *guard = p + 5 * page, *hole = p + 6 * page;
    assert(mprotect(guard, page, PROT_NONE) == 0);
    assert(avxemu_memory_query((uintptr_t)guard, &mapping) == 1);
    assert(mapping.prot == 0);
    assert(!avxemu_memory_map_gap((uintptr_t)guard, page));
    assert(avxemu_memory_read(data, (uintptr_t)guard, sizeof data) == 0);
    expect_fault(guard, 0);
    expect_fault(guard, 1);
    assert(munmap(hole, page) == 0);
    assert(avxemu_memory_query((uintptr_t)hole, &mapping) == 0);
    assert(avxemu_memory_read(data, (uintptr_t)hole, sizeof data) == 0);
    assert(avxemu_memory_read(data, (uintptr_t)guard - 16, sizeof data) == 16);
    for (int i = 0; i < 16; i++) assert(data[i] == 0x5a);
    expect_fault(hole, 0);

    avxemu_mapping saved = {11, 22, 33}, unchanged = saved;
    assert(!avxemu_memory_patch_begin((uintptr_t)guard - 1, 2, &saved));
    assert(saved.begin == unchanged.begin && saved.end == unchanged.end && saved.prot == unchanged.prot);
    assert(!avxemu_memory_patch_begin((uintptr_t)hole, 1, &saved));
    assert(!avxemu_memory_patch_begin(UINTPTR_MAX, 2, &saved));
    assert(!avxemu_memory_patch_begin((uintptr_t)p, 0, &saved));
    assert(!avxemu_memory_protect((uintptr_t)hole, page, AVXEMU_PROT_R));
    assert(!avxemu_memory_protect(UINTPTR_MAX, 2, AVXEMU_PROT_R));
    assert(!avxemu_memory_map_gap((uintptr_t)hole + 1, page));
    assert(!avxemu_memory_map_gap((uintptr_t)hole, 0));
    assert(!avxemu_memory_map_gap(UINTPTR_MAX, page));
    assert(avxemu_memory_map_gap((uintptr_t)hole, page));
    assert(avxemu_memory_query((uintptr_t)hole, &mapping) == 1);
    assert(mapping.prot == (AVXEMU_PROT_R | AVXEMU_PROT_W));
    for (size_t i = 0; i < page; i++) assert(hole[i] == 0);
    hole[0] = 0xa5;
    assert(!avxemu_memory_map_gap((uintptr_t)hole, page));
    assert(hole[0] == 0xa5);

    void *near = avxemu_memory_alloc_near((uintptr_t)p, page);
    assert(near != NULL);
    uintptr_t a = (uintptr_t)near, site = (uintptr_t)p;
    assert(a >= site ? a - site <= INT32_MAX : site - a <= INT32_MAX);
    assert(avxemu_memory_query(a, &mapping) == 1);
    assert(mapping.prot == (AVXEMU_PROT_R | AVXEMU_PROT_W | AVXEMU_PROT_X));
    memset(near, 0xcc, page);
    assert(munmap(near, page) == 0);
    assert(avxemu_memory_alloc_near(site, 0) == NULL);
    assert(avxemu_memory_alloc_near(site, SIZE_MAX) == NULL);
    assert(munmap(p, 8 * page) == 0);
    puts("memory-contract: all checks passed");
    return 0;
}
