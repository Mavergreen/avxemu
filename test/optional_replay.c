#define _GNU_SOURCE
#include "avxemu_cpuid.h"
#include "lcache.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern unsigned char embedded_cpuid[],embedded_vector[];
extern long avxemu_install_trampolines(void);
static void forged(int vector) {
    char root[]="/tmp/avxemu-optional-cache.XXXXXX";
    assert(mkdtemp(root));assert(!setenv("AVXEMU_CACHE_DIR",root,1));
    unsetenv("AVXEMU_NOCACHE");
    assert(avxemu_image_init());const avxemu_image *image=avxemu_image_main();
    unsigned char before[9];unsigned char *p=vector?embedded_vector:embedded_cpuid;
    size_t size=vector?9:3;memcpy(before,p,size);
    uint32_t range;size_t off;assert(avxemu_image_find(image,(uintptr_t)p,size,&range,&off));
    lc_site site={range,0,off},resume={range,0,off+8};
    avxemu_lc_begin();assert(avxemu_lc_mode==LC_RECORD);
    if(vector) avxemu_lc_rec_run(site,2,resume);else avxemu_lc_rec_cpuid(site);
    avxemu_lc_end();avxemu_lc_begin();assert(avxemu_lc_mode==LC_REPLAY);
    if(vector) assert(avxemu_install_trampolines()==0);
    else assert(avxemu_cpuid_patch_image(image,1)>0); /* fresh real CPUID still works */
    assert(avxemu_lc_mode==LC_OFF && !memcmp(before,p,size));
    avxemu_lc_end();
    DIR *dir=opendir(root);assert(dir);struct dirent *entry;
    while((entry=readdir(dir))) if(strstr(entry->d_name,".avxc")) {
        char path[1024];assert(snprintf(path,sizeof path,"%s/%s",root,entry->d_name)<(int)sizeof path);
        assert(!unlink(path));
    }
    closedir(dir);assert(!rmdir(root));
}
int main(int argc,char **argv) {
    int first=argc>1 && !strcmp(argv[1],"vector")?1:0;
    for(int vector=first;vector<2;vector++) {
        pid_t pid=fork();assert(pid>=0);
        if(!pid) { forged(vector);_exit(0); }
        int status;assert(waitpid(pid,&status,0)==pid);
        assert(WIFEXITED(status) && !WEXITSTATUS(status));
    }
    puts("optional replay: PASS (forged CPUID and vector data records)");return 0;
}
