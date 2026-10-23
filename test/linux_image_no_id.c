#include "image.h"
#include "platform/host.h"
#include "platform/linux/image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    assert(avxemu_image_init() && avxemu_image_main());
    int fd; uint8_t id[32], zero[32] = {0};
    assert(avxemu_linux_image_file(1,&fd,id) && !memcmp(id,zero,sizeof id));
    avxemu_file_identity identity, before; memset(&before,0x5a,sizeof before); identity=before;
    assert(!avxemu_host_identity(1,&identity) && !memcmp(&identity,&before,sizeof identity));
    assert(avxemu_host_identity(0,&identity));
    puts("linux-image-no-id: OK"); return 0;
}
