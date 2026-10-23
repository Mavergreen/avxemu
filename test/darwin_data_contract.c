#include "platform/darwin/image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    struct data_in_code_entry records[]={{0x110,8,4},{0x128,4,1}};
    avxemu_data_span spans[2],before[2]; size_t n=99;
    assert(avxemu_darwin_data_spans(records,sizeof records,0x100,0x30,spans,&n));
    assert(n==2 && spans[0].begin==16 && spans[0].end==24 && spans[1].end==44);
    memcpy(before,spans,sizeof spans);
    for(int mode=0;mode<8;mode++) {
        struct data_in_code_entry bad[2]; memcpy(bad,records,sizeof bad);
        size_t size=sizeof bad;
        if(mode==0) size--;
        if(mode==1) bad[0].offset=0xff;
        if(mode==2) bad[1].offset=0x12f;
        if(mode==3) bad[0].length=0;
        if(mode==4) bad[1].offset=0x114;
        if(mode==5) bad[0].kind=6;
        if(mode==6) bad[0].length=7;
        if(mode==7) bad[1].offset=0xffffffffu;
        n=99;
        assert(!avxemu_darwin_data_spans(bad,size,0x100,0x30,spans,&n));
        assert(n==99 && !memcmp(before,spans,sizeof spans));
    }
    assert(avxemu_darwin_data_spans(0,0,0x100,0x30,0,&n) && n==0);
    puts("darwin-data-contract: bounded, malformed, overlap and range checks OK");
    return 0;
}
