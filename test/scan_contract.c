#define _GNU_SOURCE
#include "avxemu_cpuid.h"
#include "lde.h"
#include "regfile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern void avxemu_patch_safe_test_image(const avxemu_image *image);
extern int avxemu_pool_init(void *hint, size_t capacity);
extern void *avxemu_pool_base(void);
extern void *avxemu_emit_minspill_block(const decoded *d, uint64_t resume);

int main(void) {
    assert(avxemu_zcnt_required(1u << 3, 0, 0) == 1);
    assert(avxemu_zcnt_required(0, 1u << 5, 0) == 2);
    assert(avxemu_zcnt_required(1u << 3, 1u << 5, 0) == 0);
    assert(avxemu_zcnt_required(1u << 3, 1u << 5, 1) == 3);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *mem = mmap(0, page * 4, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(mem != MAP_FAILED);
    /* This executable function returns 42, jumping over embedded CPUID and
     * LZCNT encodings which are data even though they decode cleanly. */
    static const uint8_t island[] = {
        0xeb, 6, 0x0f, 0xa2, 0xf3, 0x0f, 0xbd, 0xc0,
        0xb8, 42, 0, 0, 0, 0xc3
    };
    static const uint8_t real[] = {
        0x0f, 0xa2, 0xf3, 0x0f, 0xbd, 0xc0, 0xf3, 0x0f, 0xbc, 0xc0, 0xc3
    };
    memcpy(mem, island, sizeof island);
    uint8_t *second = mem + 2 * page;
    memcpy(second, real, sizeof real);
    avxemu_code_range ranges[] = {
        {mem, 0x1000, sizeof island, sizeof island, AVXEMU_PROT_R | AVXEMU_PROT_X},
        {second, 0x4000, 64, 64, AVXEMU_PROT_R | AVXEMU_PROT_X}
    };
    avxemu_function funcs[] = {{0, 0, sizeof island}, {1, 0, sizeof real}, {1, 32, 40}};
    avxemu_image image = {ranges, 2, funcs, 3, 0, 0, 0, AVXEMU_SCAN_STRICT};
    assert(!mprotect(mem, page * 4, PROT_READ | PROT_EXEC));
    assert(((int (*)(void))mem)() == 42);
    size_t sites[4];
    static const uint8_t indirect_data[] = {0xff, 0xe0, 0xf3, 0x0f, 0xbd, 0xc0, 0xc3};
    assert(lde_scan_sites(indirect_data, 0, sizeof indirect_data, sizeof indirect_data,
                           LDE_LZCNT, 1, sites, 4, 0) == 0);
    assert(lde_scan_sites(mem, 0, sizeof island, sizeof island,
                           LDE_LZCNT | LDE_CPUID, 1, sites, 4, 0) == 0);
    assert(lde_scan_sites(second, 0, sizeof real, sizeof real,
                           LDE_LZCNT | LDE_TZCNT | LDE_CPUID, 1, sites, 2, 0) == -1);
    assert(!memcmp(second, real, sizeof real));
    assert(lde_scan_func_mask(second, 0, sizeof real, sizeof real,
                               1, 3, 1, sites, 1, 0) == -1);
    assert(!memcmp(second, real, sizeof real)); /* overflow never edits a prefix */
    /* Production order: owned CPUID traps must fall through during later
     * recursive prefix discovery, while genuine UD2 still terminates. */
    assert(avxemu_cpuid_patch_image(&image, 1) == 1);
    assert(second[0] == 0x0f && second[1] == 0x0b);
    uint8_t code[sizeof real] = {0}, targets[sizeof real] = {0};
    int indirect;
    assert(lde_rd_map_traps(second, 0, sizeof real, sizeof real,
                            code, targets, &indirect, avxemu_cpuid_trap));
    assert(code[2] && code[6] && code[10]); /* trampoline discovery also continues */
    static const uint8_t genuine_ud2[] = {0x0f, 0x0b, 0xf3, 0x0f, 0xbd, 0xc0, 0xc3};
    assert(lde_scan_sites(genuine_ud2, 0, sizeof genuine_ud2, sizeof genuine_ud2,
                           LDE_LZCNT, 1, sites, 4, avxemu_cpuid_trap) == 0);
    assert(avxemu_patch_zcnt_image(&image, 1) == 1);
    assert(second[2] == 0xf0 && second[6] == 0xf3);
    assert(avxemu_patch_zcnt_image(&image, 2) == 1);
    assert(second[6] == 0xf0);
    assert(!memcmp(mem, island, sizeof island));
    assert(((int (*)(void))mem)() == 42);
    avxemu_regfile rf, before;
    memset(&rf, 0, sizeof rf);
    rf.rip = (uintptr_t)mem + 2; before = rf;
    assert(!avxemu_cpuid_handle(&rf) && !memcmp(&rf, &before, sizeof rf));
    rf.rip = (uintptr_t)second;
    rf.gpr[0] = 0; rf.gpr[1] = 0;
    uint32_t raw[4]; avxemu_cpuid_raw(0, 0, raw);
    assert(avxemu_cpuid_handle(&rf) && rf.rip == (uintptr_t)second + 2);
    assert(rf.gpr[0] == raw[0] && rf.gpr[3] == raw[1] &&
           rf.gpr[1] == raw[2] && rf.gpr[2] == raw[3]);
    assert(!setenv("AVXEMU_CPUID_CLR", "fma,avx2,bmi", 1));
    avxemu_cpuid_configure();
    rf.rip = (uintptr_t)second; rf.gpr[0] = 7; rf.gpr[1] = 0;
    assert(avxemu_cpuid_handle(&rf));
    assert(!(rf.gpr[3] & ((1u << 5) | (1u << 3) | (1u << 8))));
    rf.rip = (uintptr_t)second; rf.gpr[0] = 1; rf.gpr[1] = 0;
    assert(avxemu_cpuid_handle(&rf) && !(rf.gpr[1] & (1u << 12)));
    avxemu_mapping mapping;
    assert(avxemu_memory_query((uintptr_t)second, &mapping) == 1);
    assert(mapping.prot == (AVXEMU_PROT_R | AVXEMU_PROT_X));
    assert(!mprotect(second, page, PROT_READ | PROT_WRITE | PROT_EXEC));
    /* A branch after the candidate window targets its fourth byte. */
    static const uint8_t inbound[] = {0xf3, 0x0f, 0xbd, 0xc0, 0x90, 0xeb, 0xfd, 0xc3};
    memcpy(second, inbound, sizeof inbound);
    funcs[1].end = sizeof inbound;
    avxemu_patch_safe_test_image(&image);
    assert(!avxemu_patch_safe(second, 5));
    second[5] = 0x90; second[6] = 0x90;
    assert(avxemu_patch_safe(second, 5));
    assert(!avxemu_patch_safe(second + 5, 5)); /* explicit function end */
    assert(!avxemu_patch_safe(second + 16, 5)); /* unknown gap */
    assert(!avxemu_patch_safe(mem + sizeof island - 2, 5)); /* cross-range window */
    second[5] = 0xff; second[6] = 0xe0; /* unresolved jmp rax */
    assert(!avxemu_patch_safe(second, 5));
    static const uint8_t call_inbound[] = {
        0xf3, 0x0f, 0xbd, 0xc0, 0x90, 0xe8, 0xfa, 0xff, 0xff, 0xff, 0xc3
    };
    memcpy(second, call_inbound, sizeof call_inbound);
    funcs[1].end = sizeof call_inbound;
    assert(!avxemu_patch_safe(second, 5)); /* CALL targets also enter windows */
    second[5] = 0xe2; second[6] = 0xfd; /* LOOP to the fourth byte */
    second[7] = 0xc3; funcs[1].end = 8;
    assert(!avxemu_patch_safe(second, 5));
    /* Startup spans preserve different permissions across mapping boundaries. */
    size_t nsaved;
    avxemu_mapping *saved = avxemu_patch_write_begin((uintptr_t)mem + page, 2 * page, &nsaved);
    assert(saved && nsaved >= 2);
    avxemu_patch_write_end(saved, nsaved);
    assert(avxemu_memory_query((uintptr_t)mem + page, &mapping) == 1);
    assert(mapping.prot == (AVXEMU_PROT_R | AVXEMU_PROT_X));
    assert(avxemu_memory_query((uintptr_t)second, &mapping) == 1);
    assert(mapping.prot == (AVXEMU_PROT_R | AVXEMU_PROT_W | AVXEMU_PROT_X));
    /* Complete discovery precedes publication even in Darwin's linear mode:
     * a valid CPUID followed by a truncated escape must leave the range intact. */
    uint8_t *broken = mem + page;
    static const uint8_t truncated[] = {0x0f, 0xa2, 0x0f};
    assert(!mprotect(broken, page, PROT_READ | PROT_WRITE | PROT_EXEC));
    memcpy(broken, truncated, sizeof truncated);
    assert(!mprotect(broken, page, PROT_READ | PROT_EXEC));
    avxemu_code_range br = {broken, 0, sizeof truncated, sizeof truncated,
                            AVXEMU_PROT_R | AVXEMU_PROT_X};
    avxemu_function bf = {0, 0, sizeof truncated};
    avxemu_image bi = {&br, 1, &bf, 1, 0, 0, 0, AVXEMU_SCAN_STRICT};
    assert(avxemu_cpuid_patch_image(&bi, 0) == 0);
    assert(!memcmp(broken, truncated, sizeof truncated));
    assert(!avxemu_cpuid_trap((uintptr_t)broken));
    /* More CPUIDs than the fixed ownership table can hold: no unowned UD2. */
    size_t huge_size = 2 * 16385u + 1;
    uint8_t *huge = mmap(0, huge_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(huge != MAP_FAILED);
    for (size_t i = 0; i + 1 < huge_size; i += 2) { huge[i] = 0x0f; huge[i + 1] = 0xa2; }
    huge[huge_size - 1] = 0xc3;
    avxemu_code_range hr = {huge, 0, huge_size, huge_size, AVXEMU_PROT_R | AVXEMU_PROT_X};
    avxemu_function hf = {0, 0, huge_size};
    avxemu_image hi = {&hr, 1, &hf, 1, 0, 0, 0, AVXEMU_SCAN_STRICT};
    assert(avxemu_cpuid_patch_image(&hi, 1) == 0);
    for (size_t i = 0; i + 1 < huge_size; i += 2) assert(huge[i] == 0x0f && huge[i + 1] == 0xa2);
    /* A custom executable section can still belong to an ELF executable
     * segment: the real helper must remain outside the trap ownership set. */
    uint8_t *helper = (uint8_t *)(uintptr_t)avxemu_cpuid_raw;
    uint8_t helper_before[32]; memcpy(helper_before, helper, sizeof helper_before);
    avxemu_code_range rr = {helper, 0, 32, 32, AVXEMU_PROT_R | AVXEMU_PROT_X};
    avxemu_function ff = {0, 0, 32};
    avxemu_image raw_image = {&rr, 1, &ff, 1, 0, 0, 0, AVXEMU_SCAN_STRICT};
    assert(avxemu_cpuid_patch_image(&raw_image, 1) == 0);
    assert(!memcmp(helper_before, helper, sizeof helper_before));
    uint8_t declared[]={0x0f,0xa2,0xc3,0x90,0x0f,0xa2,0xf3,0x0f,0xbc,0xc0,0xc3};
    memcpy(second,declared,sizeof declared);
    avxemu_code_range dr={second,0,sizeof declared,sizeof declared,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function df={0,0,sizeof declared}; avxemu_data_span dd={0,4,sizeof declared,0};
    avxemu_image di={&dr,1,&df,1,0,&dd,1, AVXEMU_SCAN_STRICT};
    assert(avxemu_cpuid_patch_image(&di,0)==1);
    assert(second[1]==0x0b && !memcmp(second+4,declared+4,sizeof declared-4));
    assert(avxemu_patch_zcnt_image(&di,LDE_TZCNT)==0);
    assert(!memcmp(second+4,declared+4,sizeof declared-4));
    avxemu_patch_safe_test_image(&di);
    assert(!avxemu_patch_safe(second+2,5));
    assert(avxemu_patch_limit(second)==second+4);
    assert(!avxemu_patch_limit(second+4));
    assert(avxemu_pool_init(0, 1u << 20));
    decoded d;
    assert(decode(real + 2, &d) == 4);
    uint64_t unreachable = (uint64_t)(uintptr_t)avxemu_pool_base() + (UINT64_C(1) << 32);
    assert(!avxemu_emit_minspill_block(&d, unreachable));
    munmap(huge, huge_size); munmap(mem, page * 4);
    puts("scan-contract: OK");
    return 0;
}
