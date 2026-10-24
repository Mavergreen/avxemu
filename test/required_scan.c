#include "avxemu_cpuid.h"
#include "lde.h"
#include "scan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void unrelated_table(unsigned required, unsigned char modrm, int prefix) {
    unsigned char bytes[48], before[48]; memset(bytes, 0x90, sizeof bytes);
    const unsigned char entry[] = {0x48,0x8d,0x05,25,0,0,0};
    const unsigned char zcnt[] = {0xf3,0x0f,0xbd,0xc0,0xc3};
    memcpy(bytes,entry,sizeof entry);
    size_t off=7;
    if(prefix) { bytes[off++]=0x67; bytes[off++]=0x48; }
    bytes[off++]=0xff; bytes[off++]=modrm; bytes[off]=0xc3;
    memcpy(bytes+20,zcnt,sizeof zcnt);
    if(required==LDE_TZCNT) bytes[22]=0xbc;
    int32_t target=(int32_t)off-32; memcpy(bytes+32,&target,4);
    memcpy(before,bytes,sizeof bytes);
    avxemu_code_range range={bytes,0x1000,sizeof bytes,sizeof bytes,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function function={0,0,25};
    avxemu_image image={&range,1,&function,1,0, 0, 0, AVXEMU_SCAN_STRICT};
    assert(!avxemu_zcnt_preflight(&image,required));
    assert(avxemu_patch_zcnt_image(&image,required)==-1);
    assert(!memcmp(bytes,before,sizeof bytes));
    assert(avxemu_zcnt_preflight(&image,required==LDE_LZCNT?LDE_TZCNT:LDE_LZCNT));
    /* Even a table word pointing at the required bytes is only a heuristic:
     * nothing proves this unrelated indirect transfer uses that table. */
    target=20-32; memcpy(bytes+32,&target,4);
    assert(!avxemu_zcnt_preflight(&image,required));
    assert(avxemu_patch_zcnt_image(&image,required)==-1);
    assert(bytes[20]==0xf3);
}
static void covered_indirect(unsigned required, unsigned char modrm) {
    unsigned char bytes[]={0xf3,0x0f,0xbd,0xc0,0xff,modrm,0xc3};
    if(required==LDE_TZCNT) bytes[2]=0xbc;
    avxemu_code_range range={bytes,0x1000,sizeof bytes,sizeof bytes,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function function={0,0,sizeof bytes};
    avxemu_image image={&range,1,&function,1,0, 0, 0, AVXEMU_SCAN_STRICT};
    /* Like Claude's virtual calls after a reachable TZCNT, every potential
     * required encoding is already a proven instruction site. */
    assert(avxemu_zcnt_preflight(&image,required));
}

static void overlapping_prefix(int mode) {
    unsigned char bytes[64]; memset(bytes,0x90,sizeof bytes);
    bytes[0]=0x49;bytes[1]=0x31;bytes[2]=0xf3; /* actual XOR operand */
    size_t n=3,site=n; bytes[n++]=0xf3;
    if(mode==1) bytes[n++]=0x48;
    if(mode==2) bytes[n++]=0x66;
    if(mode==6) bytes[n++]=0xf2;
    if(mode==7) for(int i=0;i<9;i++) bytes[n++]=0xf3;
    if(mode==3 || mode==4) for(int i=0;i<(mode==3?5:6);i++) bytes[n++]=0x66;
    bytes[n++]=0x0f;bytes[n++]=0xbc;
    if(mode==3 || mode==4) { bytes[n++]=0x84;bytes[n++]=0x25;memset(bytes+n,0,4);n+=4; }
    else bytes[n++]=0xc0;
    bytes[n++]=0xff;bytes[n++]=0xe7;
    avxemu_code_range range={bytes,0,n,n,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function function={0,0,n}; avxemu_image image={&range,1,&function,1,0,0,0, AVXEMU_SCAN_STRICT};
    if(mode==5) { bytes[n++]=0xf3;bytes[n++]=0x0f;bytes[n++]=0xbc; range.size=range.readable=function.end=n; }
    assert(avxemu_zcnt_preflight(&image,LDE_TZCNT)==(mode<4));
    if(mode<4) {
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==1);
        assert(bytes[2]==0xf3 && bytes[site]==0xf0); /* no new operand-byte edit */
    }
}

/* Data/control-flow contradictions matter even when the source or entire
 * image contains no required encoding. Exercise both candidate-pruning levels. */
static void candidate_free_data_target(void) {
    for(int layout=0;layout<3;layout++) for(int target_zcnt=0;target_zcnt<2;target_zcnt++) {
        unsigned char bytes[48],before[48]; memset(bytes,0x90,sizeof bytes);
        size_t source=layout==2?32:0, target=layout==0?16:layout==1?40:8;
        bytes[source]=0xe9;
        int32_t delta=(int32_t)target-(int32_t)source-5;memcpy(bytes+source+1,&delta,4);
        if(layout) bytes[layout==2?0:32]=0xc3;
        if(target_zcnt) { const unsigned char tz[]={0xf3,0x0f,0xbc,0xc0};memcpy(bytes+target,tz,4); }
        bytes[target+4]=0xc3;memcpy(before,bytes,sizeof bytes);
        avxemu_code_range ranges[]={{bytes,0,layout?32:48,layout?32:48,AVXEMU_PROT_R|AVXEMU_PROT_X},
            {bytes+32,32,16,16,AVXEMU_PROT_R|AVXEMU_PROT_X}};
        avxemu_function functions[]={{0,0,layout==2?1:5},{1,0,layout==2?5:1}};
        avxemu_data_span data={(uint32_t)(layout==1?1:0),layout?8:16,layout?12:20,AVXEMU_DATA_OPAQUE};
        avxemu_image image={ranges,layout?2:1,functions,layout?2:1,0,&data,1,AVXEMU_SCAN_STRICT};
        assert(avxemu_image_data_valid(&image));
        assert(!avxemu_zcnt_preflight(&image,LDE_TZCNT));
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==-1);
        assert(!memcmp(bytes,before,sizeof bytes));
        image.strategy=AVXEMU_SCAN_LEGACY;
        assert(avxemu_zcnt_preflight(&image,LDE_TZCNT)); /* established legacy policy */
        if(!target_zcnt) {
            image.strategy=AVXEMU_SCAN_STRICT;image.data=0;image.ndata=0;
            functions[layout==2?1:0].end=1; /* truncated branch but no candidate/data */
            assert(avxemu_zcnt_preflight(&image,LDE_TZCNT)); /* no-data absence fast path */
        }
    }
}

/* External direct entries must seed the destination, including sources without
 * count candidates and targets in earlier functions or another range. */
static void external_interior_entry(void) {
    for(int cross=0;cross<2;cross++) for(int backward=0;backward<2;backward++)
    for(int source_candidate=0;source_candidate<2;source_candidate++) {
        unsigned char bytes[64],before[64]; memset(bytes,0x90,sizeof bytes);
        const unsigned char tz[]={0xf3,0x0f,0xbc,0xc0,0xc3};
        size_t source=backward?32:0, dest=backward?0:32;
        bytes[dest]=0xc3; memcpy(bytes+dest+8,tz,sizeof tz);
        size_t branch=source;
        if(source_candidate) { memcpy(bytes+source,tz,4);branch+=4; }
        bytes[branch]=0xe9;
        int32_t delta=(int32_t)(dest+8)-(int32_t)(branch+5);
        memcpy(bytes+branch+1,&delta,4);
        memcpy(bytes+48,tz,sizeof tz); memcpy(before,bytes,sizeof bytes);
        avxemu_code_range ranges[]={{bytes,0,cross?32:64,cross?32:64,5},
            {bytes+32,32,32,32,5}};
        avxemu_function f[]={{0,0,backward?13:branch+5},
            {cross?1:0,cross?0:32,(cross?0:32)+(backward?branch+5-32:13)},
            {cross?1:0,cross?16:48,cross?21:53}};
        avxemu_image image={ranges,cross?2:1,f,3,0,0,0,AVXEMU_SCAN_STRICT};
        assert(avxemu_zcnt_preflight(&image,LDE_TZCNT));
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==2+source_candidate);
        assert(bytes[dest+8]==0xf0 && bytes[48]==0xf0);
        memcpy(bytes,before,sizeof bytes);
        avxemu_scan *scan=avxemu_scan_prepare(&image,0,1);
        assert(!scan && !memcmp(bytes,before,sizeof bytes));
        /* An independently decoded instruction overlaps the external root. */
        bytes[dest]=0x48;bytes[dest+1]=0xb8; /* ten-byte MOV immediate covers target */
        memcpy(before,bytes,sizeof bytes);
        assert(!avxemu_zcnt_preflight(&image,LDE_TZCNT));
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==-1);
        assert(!memcmp(bytes,before,sizeof bytes));
    }
}

static void chained_entries(void) {
    for(int cross=0;cross<2;cross++) for(int backward=0;backward<2;backward++) {
        unsigned char bytes[96],before[96];memset(bytes,0x90,sizeof bytes);
        /* Source -> candidate-free middle's hidden entry -> required site. */
        const unsigned char tz[]={0xf3,0x0f,0xbc,0xc0,0xc3};
        size_t source=backward?64:0,dest=backward?0:64;
        bytes[dest]=bytes[32]=0xc3;memcpy(bytes+dest+8,tz,sizeof tz);
        bytes[40]=0xe9;int32_t next=(int32_t)dest+8-45;memcpy(bytes+41,&next,4);
        bytes[source]=0xe9;int32_t target=40-(int32_t)source-5;memcpy(bytes+source+1,&target,4);
        memcpy(bytes+80,tz,sizeof tz);memcpy(before,bytes,sizeof bytes);
        avxemu_code_range r[]={{bytes,0,cross?32:96,cross?32:96,5},
            {bytes+32,32,32,32,5},{bytes+64,64,32,32,5}};
        avxemu_function f[]={{0,0,backward?13:5},
            {cross?1:0,cross?0:32,cross?13:45},
            {cross?2:0,cross?0:64,(cross?0:64)+(backward?5:13)},
            {cross?2:0,cross?16:80,cross?21:85}};
        avxemu_image image={r,cross?3:1,f,4,0,0,0,AVXEMU_SCAN_STRICT};
        /* Initial visits fit; backward roots need revisits. Exhaustion must
         * not return a partially authorized map or change any bytes. */
        avxemu_scan *scan=avxemu_scan_prepare(&image,0,36);
        assert(!!scan==!backward);avxemu_scan_free(scan);
        assert(!memcmp(bytes,before,sizeof bytes));
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==2);
        assert(bytes[dest+8]==0xf0 && bytes[80]==0xf0);
        memcpy(bytes,before,sizeof bytes);
        avxemu_data_span data={(uint32_t)(cross && !backward?2:0),cross?8:dest+8,cross?12:dest+12,AVXEMU_DATA_OPAQUE};
        image.data=&data;image.ndata=1;
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==-1);
        assert(!memcmp(bytes,before,sizeof bytes));
    }
}

static void cross_range_data_target(void) {
    for(int backward=0;backward<2;backward++) {
        unsigned char bytes[48]; memset(bytes,0x90,sizeof bytes);
        const unsigned char tz[]={0xf3,0x0f,0xbc,0xc0};
        size_t source=backward?32:0, target=backward?8:40;
        memcpy(bytes+source,tz,4);bytes[source+4]=0xe9;
        int32_t delta=(int32_t)target-(int32_t)source-9;memcpy(bytes+source+5,&delta,4);
        bytes[backward?0:32]=0xc3;memcpy(bytes+target,tz,4);
        avxemu_code_range r[]={{bytes,0,32,32,AVXEMU_PROT_R|AVXEMU_PROT_X},
            {bytes+32,32,16,16,AVXEMU_PROT_R|AVXEMU_PROT_X}};
        avxemu_function f[]={{0,0,backward?1:9},{1,0,backward?9:1}};
        avxemu_data_span data={(uint32_t)(backward?0:1),8,12,AVXEMU_DATA_OPAQUE};
        avxemu_image image={r,2,f,2,0,&data,1,AVXEMU_SCAN_STRICT};
        assert(!avxemu_zcnt_preflight(&image,LDE_TZCNT));
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==-1 && bytes[source]==0xf3);
    }
}

/* Prepared startup plans are caller-owned and consumed once. A changed
 * planned original rejects the entire transaction before the first edit. */
static void prepared_plan(void) {
    unsigned char bytes[]={0xf3,0x0f,0xbc,0xc0,0xf3,0x0f,0xbc,0xc0,0xc3};
    avxemu_code_range r={bytes,0,sizeof bytes,sizeof bytes,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function f={0,0,sizeof bytes};
    avxemu_image image={&r,1,&f,1,0,0,0,AVXEMU_SCAN_STRICT};
    avxemu_zcnt_plan *plan=avxemu_zcnt_prepare(&image,LDE_TZCNT);
    assert(plan); bytes[4]=0x90;
    assert(avxemu_zcnt_apply(plan)==-1 && bytes[0]==0xf3);
    bytes[4]=0xf3;
    plan=avxemu_zcnt_prepare(&image,LDE_TZCNT); assert(plan);
    assert(avxemu_zcnt_apply(plan)==2 && bytes[0]==0xf0 && bytes[4]==0xf0);
    assert(avxemu_zcnt_preflight(&image,LDE_TZCNT)); /* fresh, no stale plan */
}

static void strategy_policy(void) {
    unsigned char bytes[64]; memset(bytes,0x90,sizeof bytes);
    const unsigned char tz[]={0xf3,0x0f,0xbc,0xc0};
    bytes[0]=0xeb;bytes[1]=6;memcpy(bytes+2,tz,4);memcpy(bytes+8,tz,4);bytes[12]=0xc3;
    memcpy(bytes+48,tz,4);
    avxemu_code_range r={bytes,0,sizeof bytes,sizeof bytes,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function f={0,0,13};avxemu_data_span data={0,48,56,AVXEMU_DATA_OPAQUE};
    avxemu_image image={&r,1,&f,1,0,&data,1,AVXEMU_SCAN_STRICT};
    for(int strategy=0;strategy<2;strategy++) {
        image.strategy=(avxemu_scan_strategy)strategy;
        assert(avxemu_zcnt_preflight(&image,LDE_TZCNT));
        assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==1);
        assert(bytes[2]==0xf3 && bytes[8]==0xf0 && bytes[48]==0xf3);
        bytes[8]=0xf3;
    }
    memcpy(bytes+32,tz,4); /* Cold gap: only the established legacy mop-up selects it. */
    image.strategy=AVXEMU_SCAN_STRICT;
    assert(!avxemu_zcnt_preflight(&image,LDE_TZCNT));
    assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==-1 && bytes[32]==0xf3);
    image.strategy=AVXEMU_SCAN_LEGACY;
    assert(avxemu_zcnt_preflight(&image,LDE_TZCNT));
    assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==2);
    assert(bytes[2]==0xf3 && bytes[8]==0xf0 && bytes[32]==0xf0 && bytes[48]==0xf3);
}
static void coalesced_unproven(void) {
    unsigned char bytes[64];memset(bytes,0x90,sizeof bytes);
    const unsigned char entry[]={0x48,0x8d,0x3d,41,0,0,0,0xff,0xe7};
    const unsigned char tz[]={0xf3,0x0f,0xbc,0xc0,0xc3};
    memcpy(bytes,entry,sizeof entry);memcpy(bytes+20,tz,sizeof tz);
    int32_t tables[]={9-48,9-48,20-48,9-48};memcpy(bytes+48,tables,sizeof tables);
    avxemu_code_range r={bytes,0,sizeof bytes,sizeof bytes,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function f={0,0,sizeof bytes};avxemu_data_span d={0,48,64,AVXEMU_DATA_JUMP32};
    avxemu_image image={&r,1,&f,1,0,&d,1,AVXEMU_SCAN_STRICT};
    assert(!avxemu_zcnt_preflight(&image,LDE_TZCNT));
    assert(avxemu_patch_zcnt_image(&image,LDE_TZCNT)==-1 && bytes[20]==0xf3);
}

int main(void) {
    external_interior_entry();chained_entries();
    candidate_free_data_target();
    cross_range_data_target();
    prepared_plan();
    strategy_policy(); coalesced_unproven();
    for(int mode=0;mode<8;mode++) overlapping_prefix(mode);
    {
        unsigned char interior[]={0x48,0xb8,0xf3,0x0f,0xbc,0xc0,0,0,0,0,
            0xf3,0x0f,0xbc,0xc0,0xff,0xe7};
        avxemu_code_range r={interior,0,sizeof interior,sizeof interior,AVXEMU_PROT_R|AVXEMU_PROT_X};
        avxemu_function f={0,0,sizeof interior}; avxemu_image image={&r,1,&f,1,0,0,0, AVXEMU_SCAN_STRICT};
        assert(!avxemu_zcnt_preflight(&image,LDE_TZCNT)); /* unrelated immediate stays unknown */
    }
    /* An unrelated LEA plus a plausible table entry never proves where an
     * indirect call/jump goes. Cover register/memory and prefixed encodings. */
    const unsigned char transfers[]={0xe7,0x27,0xd7,0x17,0x2f,0x1f};
    for(unsigned i=0;i<sizeof transfers;i++) for(int prefix=0;prefix<2;prefix++) {
        unrelated_table(LDE_LZCNT,transfers[i],prefix);
        unrelated_table(LDE_TZCNT,transfers[i],prefix);
        covered_indirect(LDE_LZCNT,transfers[i]);
        covered_indirect(LDE_TZCNT,transfers[i]);
    }
    unsigned char known[]={0xf3,0x0f,0xbc,0xc0,0xe8,0x00,0x01,0x00,0x00,
                           0xf3,0x0f,0xbc,0xc0,0xbf,0xff,0xff,0xff};
    avxemu_code_range or={known,0x1000,sizeof known,sizeof known,AVXEMU_PROT_R|AVXEMU_PROT_X};
    avxemu_function of={0,0,sizeof known};
    avxemu_data_span data={0,9,sizeof known,0};
    avxemu_image oi={&or,1,&of,1,0,&data,1, AVXEMU_SCAN_STRICT};
    assert(avxemu_zcnt_preflight(&oi,LDE_TZCNT)); /* authoritative table, not decoded data */
    known[4]=0xe9; known[5]=0; known[6]=known[7]=known[8]=0;
    assert(!avxemu_zcnt_preflight(&oi,LDE_TZCNT)); /* direct branch into declared data */
    known[4]=0xe8; known[5]=0; known[6]=1;
    data.begin=3;
    assert(!avxemu_zcnt_preflight(&oi,LDE_TZCNT)); /* decoded instruction crosses data */
    oi.data=0; oi.ndata=0;
    unsigned char padding[32]={0xf3,0x0f,0xbc,0xc0,0xe8,0x00,0x01,0x00,0x00};
    or.base=padding; or.size=or.readable=22; of.end=22;
    assert(avxemu_zcnt_preflight(&oi,LDE_TZCNT)); /* call, then 13 zero alignment bytes */
    or.size=or.readable=26; of.end=26;
    assert(!avxemu_zcnt_preflight(&oi,LDE_TZCNT)); /* 17-byte tail exceeds allowance */
    or.size=or.readable=22; of.end=22; padding[21]=0xe9;
    assert(!avxemu_zcnt_preflight(&oi,LDE_TZCNT)); /* not a padding-only suffix */
    padding[21]=0; padding[9]=0xe9;
    or.size=or.readable=13; of.end=13;
    assert(!avxemu_zcnt_preflight(&oi,LDE_TZCNT)); /* zero displacement cannot hide truncation */
    unsigned char bytes[48]; memset(bytes, 0x90, sizeof bytes);
    const unsigned char lz[] = {0xf3, 0x0f, 0xbd, 0xc0, 0xc3};
    const unsigned char tz[] = {0xf3, 0x0f, 0xbc, 0xc0, 0xc3};
    avxemu_code_range r = {bytes, 0x400200, sizeof bytes, sizeof bytes, AVXEMU_PROT_R | AVXEMU_PROT_X};
    avxemu_function f = {0, 0, 8};
    avxemu_image im = {&r, 1, &f, 1, 0, 0, 0, AVXEMU_SCAN_STRICT};
    bytes[0] = 0xc3;
    assert(avxemu_zcnt_preflight(&im, 3));
    memcpy(bytes + 32, lz, sizeof lz);
    assert(!avxemu_zcnt_preflight(&im, 1)); /* partial metadata, unknown gap */
    assert(avxemu_zcnt_preflight(&im, 2));
    im.nfunctions = 0;
    assert(!avxemu_zcnt_preflight(&im, 1)); /* missing metadata */
    memcpy(bytes + 32, tz, sizeof tz);
    assert(avxemu_zcnt_preflight(&im, 1));
    assert(!avxemu_zcnt_preflight(&im, 2));
    memset(bytes, 0x90, sizeof bytes); im.nfunctions = 1;
    memcpy(bytes + 2, lz, sizeof lz); bytes[0] = 0xeb; bytes[1] = 5; bytes[7] = 0xc3;
    assert(avxemu_zcnt_preflight(&im, 3)); /* resolved jump over data */
    assert(avxemu_patch_zcnt_image(&im, 3) == 0); /* proven data stays unchanged on every OS */
    assert(bytes[2] == 0xf3);
    bytes[0] = 0xff; bytes[1] = 0xe0;
    assert(!avxemu_zcnt_preflight(&im, 1)); /* unresolved indirect target */
    assert(avxemu_zcnt_preflight(&im, 2)); /* conservative absence */
    bytes[0] = 0xe9; f.end = 4; /* unsupported decode cannot establish completeness */
    assert(!avxemu_zcnt_preflight(&im, 1));
    f.end = 8; memcpy(bytes, lz, sizeof lz); memset(bytes + 5, 0x90, 3);
    assert(avxemu_zcnt_preflight(&im, 3));
    bytes[4] = 0xe9; /* decoded required site followed by a truncated branch */
    assert(!avxemu_zcnt_preflight(&im, 1));
    bytes[4] = 0xc3;
    f.end = sizeof bytes + 1;
    assert(!avxemu_zcnt_preflight(&im, 1));
    assert(avxemu_zcnt_preflight(0, 0));
    puts("required-scan: gaps, bounds, resolved and unresolved flow OK");
}
