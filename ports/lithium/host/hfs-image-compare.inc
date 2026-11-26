/* Appended to the regular-image, bounded mmap I/O in hfs-root-copy.c. */
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    TestByteOrder();io_func *a=mapped_open(argv[1],0),*b=mapped_open(argv[2],0);
    Volume *va=openVolume(a),*vb=openVolume(b);if(!va||!vb)return 3;
    FILE *f=fopen(argv[3],"rb");if(!f)return 4;
    char magic[8];if(fread(magic,1,8,f)!=8||memcmp(magic,"MIXHFS01",8))return 5;
    struct input in;uint64_t count=0,bytes=0;
    unsigned char *x=malloc(1048576),*y=malloc(1048576);if(!x||!y)return 6;
    while(fread(&in,1,sizeof(in),f)==sizeof(in)) {
        if(!in.plen||in.plen>65535||in.slen>65535||in.llen>65535)return 7;
        char *path=calloc(1,in.plen+1);if(!path)return 8;
        if(fread(path,1,in.plen,f)!=in.plen||fseek(f,in.slen+in.llen,SEEK_CUR))return 9;
        HFSPlusCatalogRecord *r=getRecordFromPath3(path,va,NULL,NULL,0,0,kHFSRootFolderID);
        HFSPlusCatalogRecord *s=getRecordFromPath3(path,vb,NULL,NULL,0,0,kHFSRootFolderID);
        if(!r||!s||r->recordType!=s->recordType)die("original catalog type",path);
        if(r->recordType==kHFSPlusFolderRecord) {
            HFSPlusCatalogFolder *ra=(void*)r,*rb=(void*)s;
            if(ra->folderID!=rb->folderID||memcmp(&ra->permissions,&rb->permissions,sizeof(ra->permissions)))die("directory metadata",path);
            uint32_t extra=!strcmp(path,"/private/preboot")?1:0;
            if(rb->valence!=ra->valence+extra)die("unexpected directory entries",path);
        } else if(r->recordType==kHFSPlusFileRecord) {
            HFSPlusCatalogFile *ra=(void*)r,*rb=(void*)s;
            if(memcmp(ra,rb,sizeof(*ra)))die("original file metadata/forks changed",path);
            io_func *fa=openRawFile(ra->fileID,&ra->dataFork,r,va);
            io_func *fb=openRawFile(rb->fileID,&rb->dataFork,s,vb);
            for(uint64_t off=0;off<ra->dataFork.logicalSize;) {
                size_t n=ra->dataFork.logicalSize-off;if(n>1048576)n=1048576;
                if(!READ(fa,off,n,x)||!READ(fb,off,n,y)||memcmp(x,y,n))die("original bytes changed",path);
                off+=n;bytes+=n;
            }
            CLOSE(fa);CLOSE(fb);
        } else die("unsupported original record",path);
        free(r);free(s);free(path);count++;
        if(count%25000==0){printf("verified original entries=%llu\n",(unsigned long long)count);fflush(stdout);}
    }
    if(!feof(f))return 10;
    fclose(f);free(x);free(y);closeVolume(va);closeVolume(vb);CLOSE(a);CLOSE(b);
    printf("original catalog/file data matched: entries=%llu bytes=%llu\n",(unsigned long long)count,(unsigned long long)bytes);
    return count==329334?0:11;
}
