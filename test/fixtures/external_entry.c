#include <stdio.h>
extern unsigned char side_entry[];
extern int count_from_other(void);
int main(void) {
    printf("cross-function result=%d, required prefix=%02x\n",count_from_other(),side_entry[0]);
    return side_entry[0]!=0xf0;
}
