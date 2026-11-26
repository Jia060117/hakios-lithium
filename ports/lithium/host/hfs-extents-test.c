/* Real interleaved allocations force 24 extents per fork, then reopen them. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hfs/hfsplus.h>
char endianness;
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    uint16_t one=1;endianness=*(uint8_t*)&one?IS_LITTLE_ENDIAN:IS_BIG_ENDIAN;
    io_func *image=openFlatFile(argv[1]);Volume *v=openVolume(image);
    HFSCatalogNodeID a=newFile("/a",v),b=newFile("/b",v);
    if(!a || !b)return 3;
    HFSPlusCatalogFile *fa=(HFSPlusCatalogFile*)getRecordByCNID(a,v);
    HFSPlusCatalogFile *fb=(HFSPlusCatalogFile*)getRecordByCNID(b,v);
    io_func *ia=openRawFile(a,&fa->dataFork,(HFSPlusCatalogRecord*)fa,v);
    io_func *ib=openRawFile(b,&fb->dataFork,(HFSPlusCatalogRecord*)fb,v);
    unsigned char buffer[4096];
    for(unsigned n=0;n<24;n++) {
        memset(buffer,n+1,sizeof(buffer));if(!WRITE(ia,n*4096,sizeof(buffer),buffer))return 4;
        memset(buffer,n+65,sizeof(buffer));if(!WRITE(ib,n*4096,sizeof(buffer),buffer))return 5;
    }
    CLOSE(ia);CLOSE(ib);free(fa);free(fb);closeVolume(v);CLOSE(image);
    image=openFlatFileRO(argv[1]);v=openVolume(image);
    for(unsigned file=0;file<2;file++) {
        HFSPlusCatalogFile *f=(HFSPlusCatalogFile*)getRecordByCNID(file?b:a,v);
        io_func *io=openRawFile(f->fileID,&f->dataFork,(HFSPlusCatalogRecord*)f,v);
        if(!io || f->dataFork.logicalSize!=24*4096)return 6;
        for(unsigned n=0;n<24;n++) {
            if(!READ(io,n*4096,sizeof(buffer),buffer))return 7;
            for(unsigned j=0;j<4096;j++)if(buffer[j]!=(file?n+65:n+1))return 8;
        }
        CLOSE(io);free(f);
    }
    closeVolume(v);CLOSE(image);puts("two 24-extent forks reopened and contents verified");return 0;
}
