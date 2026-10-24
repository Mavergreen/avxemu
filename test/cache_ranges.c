/* Cache format and replay contract with two independent executable ranges. */
#define _DARWIN_C_SOURCE
#define _GNU_SOURCE
#include "lcache.h"
#include "avxemu_cpuid.h"
#include "lde.h"
#include "platform/host.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static avxemu_image image;
static int identity_change;
static char *settings[] = {"AVXEMU_ALPHA=1", "AVXEMU_BETA=2", 0};
static char directory[512], path[1024];
const avxemu_image *avxemu_image_main(void) { return &image; }
int avxemu_host_identity(int self, avxemu_file_identity *id) {
    memset(id, 0, sizeof *id); id->ino = self ? 2 : 1;
    if (identity_change == 11 && self) id->dev = 1;
    if (identity_change == 12 && !self) id->mtime_ns = 1;
    return 1;
}
char **avxemu_host_environ(void) { return settings; }
int avxemu_host_cache_dir(char *dst, size_t n) {
    int k = snprintf(dst, n, "%s", directory); return k >= 0 && (size_t)k < n;
}
static uint32_t checksum(const void *p, size_t n) {
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    const uint8_t *b = p;
    while (n--) { h ^= *b++; h *= UINT64_C(0x100000001b3); }
    return (uint32_t)h;
}
static void locate(void) {
    DIR *d = opendir(directory); assert(d);
    struct dirent *e; int found = 0;
    while ((e = readdir(d))) if (strstr(e->d_name, ".avxc")) {
        assert(snprintf(path, sizeof path, "%s/%s", directory, e->d_name) < (int)sizeof path); found++;
    }
    closedir(d); assert(found == 1);
}
static void mutate(int which) {
    int fd = open(path, O_RDWR); assert(fd >= 0);
    struct stat st; assert(!fstat(fd, &st));
    uint8_t *data = malloc((size_t)st.st_size); assert(data);
    assert(read(fd, data, (size_t)st.st_size) == st.st_size);
    lc_hdr *h = (lc_hdr *)data;
    if (which == 0) h->version = 1;
    if (which == 1) h->n_bytes = UINT32_MAX;
    if (which == 15) ((lc_byte *)(data + sizeof *h))[1].reserved[5] = 1;
    if (which == 16) ((lc_byte *)(data + sizeof *h))[1].site = ((lc_byte *)(data + sizeof *h))[0].site;
    h->body_sum = checksum(data + sizeof *h, (size_t)st.st_size - sizeof *h);
    assert(lseek(fd, 0, SEEK_SET) == 0);
    assert(write(fd, data, (size_t)st.st_size) == st.st_size);
    assert(!close(fd)); free(data);
}
static void fixture(int which) {
    int change_strategy=which==29;
    int legacy_missing=which==32, legacy_opcode=which==35, legacy_need=which==36, legacy_cpuid=which==38, legacy_data=which==39, legacy_window=which==40;
    if(which>=32) {
        image.strategy=AVXEMU_SCAN_LEGACY;
        which=which==32?17:which==33?7:which==34?19:which==35?6:which==36?6:which==38?8:which>=39?25:26;
    }
    if(which==30 || which==31) {
        image.strategy=AVXEMU_SCAN_LEGACY;
        which=which==30?6:25;
    }
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *mem = mmap(0, page * 3, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(mem != MAP_FAILED); memset(mem, 0x90, page * 3);
    mem[0] = mem[2 * page] = 0x0f; mem[1] = mem[2 * page + 1] = 0xa2;
    avxemu_code_range ranges[] = {
        {mem, 0x1000, 16, 16, AVXEMU_PROT_R | AVXEMU_PROT_X},
        {mem + 2 * page, 0x4000, 16, 16, AVXEMU_PROT_R | AVXEMU_PROT_X}
    };
    static const uint8_t lz[] = {0xf3, 0x0f, 0xbd, 0xc0, 0xc3};
    memcpy(mem + 4, lz, sizeof lz); memcpy(mem + 2 * page + 4, lz, sizeof lz);
    avxemu_function functions[] = {{0, 0, 9}, {1, 0, 9}};
    if((which==21 || which==22)) {
        uint8_t *second=mem+2*page;
        static const uint8_t entry[]={0x48,0x8d,0x05,25,0,0,0,0xff,0xe7,0xc3};
        memset(second,0x90,48); memcpy(second,entry,sizeof entry);
        memcpy(second+20,lz,sizeof lz);
        int32_t target=-23; memcpy(second+32,&target,4);
        if(which==22) { mem[6]=0xbc; second[22]=0xbc; }
        ranges[1].size=ranges[1].readable=48; functions[1].end=25;
    }
    if(which==23 || which==24) {
        uint8_t *second=mem+2*page;
        second[8]=0xff; second[9]=0x50; second[10]=0x20; second[11]=0xc3;
        functions[1].end=12;
        if(which==24) { mem[6]=0xbc; second[6]=0xbc; }
    }
    avxemu_data_span data={1,4,14,0};
    if(which>=25 && which<=27) {
        image.data=&data; image.ndata=1;
        mem[2*page+12]=0x0f; mem[2*page+13]=0xa2;
        if(which==27) { data.begin=8; data.end=12; }
        if(legacy_window) { data.begin=7; data.end=8; }
    }
    image.ranges = ranges; image.nranges = 2;
    image.functions = functions; image.nfunctions = 2;
    char root[] = "/tmp/avxemu-ranges.XXXXXX"; assert(mkdtemp(root));
    assert(snprintf(directory, sizeof directory, "%s/absent/Library/Caches/avxemu", root) < (int)sizeof directory);
    unsetenv("AVXEMU_NOCACHE");
    avxemu_lc_begin(); assert(avxemu_lc_mode == LC_RECORD);
    lc_site first = {0, 0, 4}, second = {1, 0, 4};
    if ((which == 21 || which == 22)) second.off = 20;
    if (which == 2) second.off = 16;        /* offset at end */
    if (which == 3) second.range = 2;      /* unknown range */
    if (which == 4) second.off = UINT64_MAX;
    if (which == 5) {
        lc_site start = {0, 0, 0}, resume = {1, 0, 8};
        avxemu_lc_rec_run(start, 1, resume);
    } else if(which==26) {
        lc_site site={1,0,12}; avxemu_lc_rec_cpuid(site);
    } else if(which==27) {
        lc_site site={1,0,0}, resume={1,0,14}; avxemu_lc_rec_run(site,2,resume);
    } else if (which == 8) {
        lc_site a = {0, 0, 0}, b = {1, 0, 0};
        if(legacy_cpuid) { b.off=12; mem[2*page+12]=0x0f; mem[2*page+13]=0x90; }
        else mem[2 * page + 1] = 0x90; /* final CPUID disagrees */
        avxemu_lc_rec_cpuid(a); avxemu_lc_rec_cpuid(b);
    } else {
        avxemu_lc_rec_byte(first, 0xf3, 0xf0);
        if (which == 20) second.off = 12;
        if ((which != 17 && which != 25) || legacy_data || legacy_window) avxemu_lc_rec_byte(second, which == 7 ? 0x91 : 0xf3, which == 19 ? 0x90 : 0xf0);
        if (which == 18) {
            lc_site extra = {1, 0, 12}; avxemu_lc_rec_byte(extra, 0x90, 0xf0);
        }
    }
    avxemu_lc_end(); locate();
    if (which <= 1 || which >= 15) mutate(which);
    if(legacy_opcode) mem[2*page+6]=0xaf; /* F3 remains but no longer ZCNT */
    if(legacy_need) mem[2*page+6]=0xbc; /* valid opcode, wrong current need */
    if (which == 9) {
        uint8_t *moved = mmap(0, page * 3, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANON, -1, 0);
        assert(moved != MAP_FAILED && moved != mem);
        memcpy(moved, mem, page * 3);
        assert(!munmap(mem, page * 3)); mem = moved;
        ranges[0].base = mem; ranges[1].base = mem + 2 * page;
    }
    if (which == 10) ranges[1].vaddr++;
    if (which == 11 || which == 12) identity_change = which;
    if (which == 13) { char *s = settings[0]; settings[0] = settings[1]; settings[1] = s; }
    if (which == 14) settings[1] = "AVXEMU_BETA=3";
    if(change_strategy) image.strategy=AVXEMU_SCAN_LEGACY;
    if(which==28) { data.begin=12; data.end=14; image.data=&data; image.ndata=1; }
    assert(!mprotect(mem, page * 3, PROT_READ | PROT_EXEC));
    avxemu_lc_begin();
    if (legacy_data || which <= 5 || which>=26 || (which >= 10 && which <= 16 && which != 13)) {
        assert(avxemu_lc_mode == LC_RECORD); /* malformed files are misses */
        avxemu_lc_rec_fail();
    } else {
        assert(avxemu_lc_mode == LC_REPLAY);
        if ((which == 21 || which == 22)) {
            uint8_t before[48]; memcpy(before,mem+2*page,sizeof before);
            assert(avxemu_patch_zcnt_image(&image,which==21?LDE_LZCNT:LDE_TZCNT)==-1);
            assert(mem[4]==0xf3 && !memcmp(before,mem+2*page,sizeof before));
        } else if (which == 8) {
            avxemu_zcnt_plan *prepared=legacy_cpuid?avxemu_zcnt_prepare(&image,LDE_LZCNT):0;
            assert(!legacy_cpuid || prepared);
            assert(avxemu_cpuid_patch_image(&image, 1) == (legacy_cpuid?2:0));
            if(prepared) assert(avxemu_zcnt_apply(prepared)==2 && mem[4]==0xf0 && mem[2*page+4]==0xf0);
            assert(avxemu_cpuid_trap((uintptr_t)mem)==legacy_cpuid);
            assert(avxemu_cpuid_trap((uintptr_t)(mem + 2 * page))==legacy_cpuid);
            /* Strict optional discovery declines the entire malformed image;
             * legacy retains its established per-range fallback. */
            assert(mem[1] == (legacy_cpuid?0x0b:0xa2));
        } else {
            long n = avxemu_patch_zcnt_image(&image, which==24?LDE_TZCNT:LDE_LZCNT);
            if(which>=23 && !legacy_window) assert(avxemu_lc_mode==LC_REPLAY);
            assert(n == (which==25 || legacy_missing || legacy_opcode || legacy_need?1:2)); /* rejected replay falls back to proven fresh sites */
            assert(mem[4] == 0xf0 && mem[2 * page + 4] == (which==25 || legacy_missing || legacy_opcode || legacy_need?0xf3:0xf0));
            assert(mem[12] == 0x90 && mem[2 * page + 12] == (which==25?0x0f:0x90));
        }
        if(legacy_missing) assert(avxemu_lc_mode==LC_REPLAY); /* documented legacy completeness trust */
        else if (legacy_window || legacy_opcode || legacy_need || which == 7 || which == 8 || (which >= 17 && which <= 20)) assert(avxemu_lc_mode == LC_OFF);
    }
    assert(mem[0] == 0x0f && mem[2 * page] == ((which==21 || which==22)?0x48:0x0f));
    if (legacy_data || which <= 5 || which>=26 || (which >= 10 && which <= 16 && which != 13)) {
        assert(mem[4] == 0xf3 && mem[2 * page + 4] == 0xf3);
        assert(!avxemu_cpuid_trap((uintptr_t)mem) && !avxemu_cpuid_trap((uintptr_t)(mem + 2 * page)));
    }
    avxemu_lc_end(); munmap(mem, page * 3);
    unlink(path); rmdir(directory);
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); assert(system(cmd) == 0);
}
/* A locally valid CPUID cache record is not authority to patch a guessed
 * table root. Strict replay must use the same direct roots as cold analysis. */
static void optional_replay(void) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *bytes=mmap(0,page,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(bytes!=MAP_FAILED); memset(bytes,0x90,48);
    const uint8_t entry[]={0x48,0x8d,0x15,25,0,0,0,0xff,0xe7};
    memcpy(bytes,entry,sizeof entry); bytes[20]=0x0f;bytes[21]=0xa2;bytes[22]=0xc3;
    int32_t target=20-32;memcpy(bytes+32,&target,4);
    avxemu_code_range range={bytes,0,48,48,5}; avxemu_function f={0,0,48};
    image=(avxemu_image){&range,1,&f,1,0,0,0,AVXEMU_SCAN_STRICT};
    char root[]="/tmp/avxemu-optional-replay.XXXXXX";assert(mkdtemp(root));
    assert(snprintf(directory,sizeof directory,"%s",root)<(int)sizeof directory);
    unsetenv("AVXEMU_NOCACHE"); avxemu_lc_begin();assert(avxemu_lc_mode==LC_RECORD);
    lc_site site={0,0,20}; avxemu_lc_rec_cpuid(site);avxemu_lc_end();locate();
    avxemu_lc_begin();assert(avxemu_lc_mode==LC_REPLAY);
    assert(avxemu_cpuid_patch_image(&image,1)==0);
    assert(bytes[20]==0x0f && bytes[21]==0xa2 && !avxemu_cpuid_trap((uintptr_t)bytes+20));
    assert(avxemu_lc_mode==LC_OFF);avxemu_lc_end();
    munmap(bytes,page);unlink(path);rmdir(root);
}
int main(void) {
    pid_t probe=fork();assert(probe>=0);
    if(!probe) { optional_replay();_exit(0); }
    int probe_status;assert(waitpid(probe,&probe_status,0)==probe);
    assert(WIFEXITED(probe_status) && !WEXITSTATUS(probe_status));
    static const char *names[] = {"v1", "overflow count", "offset at end", "invalid range",
        "offset overflow", "cross-range resume", "range 1 replay", "final byte invalid", "final CPUID invalid", "different load addresses",
        "changed range layout", "changed emulator identity", "changed executable identity",
        "reordered settings", "changed settings", "reserved byte", "duplicate byte site",
        "missing required record", "extra unknown-data record", "wrong replacement", "unknown-data destination", "unproven indirect LZCNT replay", "unproven indirect TZCNT replay", "covered indirect LZCNT replay", "covered indirect TZCNT replay", "declared-data prefix preserved", "CPUID in declared data", "run crossing declared data", "changed data layout", "changed scan strategy", "legacy replay", "legacy declared-data replay", "legacy completeness trust", "legacy bad original fallback", "legacy wrong replacement fallback", "legacy wrong opcode fallback", "legacy wrong need mask fallback", "legacy declared-data malformed record", "legacy prerequisite invalidates prepared cache", "legacy ZCNT in declared data", "legacy decoded window crosses data"};
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        pid_t pid = fork(); assert(pid >= 0);
        if (!pid) { fixture(i); _exit(0); }
        int status; assert(waitpid(pid, &status, 0) == pid);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "cache-ranges: %s failed\n", names[i]); return 1;
        }
    }
    puts("cache-ranges: OK (42 fixtures)"); return 0;
}
