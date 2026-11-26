/* Offline HFS+ import/verification using pinned GPLv3 libdmg-hfsplus.
 * Manifest paths are normalized to HFS's Unicode form by the Python builder.
 * No host block device or phone is opened: argv[1] must be a regular image.
 */
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <hfs/hfsplus.h>
#include <hfs/hfslib.h>
#include <abstractfile.h>
char endianness;
static void TestByteOrder(void) { uint16_t n=1;endianness=*(uint8_t*)&n?IS_LITTLE_ENDIAN:IS_BIG_ENDIAN; }
struct input { uint32_t plen,slen,llen,mode,uid,gid,mtime,atime; uint64_t size; };
static void die(const char *what,const char *path) {fprintf(stderr,"%s: %s\n",what,path);exit(1);}
/* The fixed-size image fits this 64-bit host's address space. Bounded mmap
 * avoids millions of fseek/fread transitions during catalog operations.
 * It changes only byte I/O, not the pinned library's filesystem algorithms. */
struct mapped_image { unsigned char *base; size_t size; int fd,writable; };
static int mapped_read(io_func *io,off_t pos,size_t n,void *data) {
    struct mapped_image *m=io->data;
    if(pos<0 || (uint64_t)pos>m->size || n>m->size-(size_t)pos)return 0;
    memcpy(data,m->base+(size_t)pos,n);return 1;
}
static int mapped_write(io_func *io,off_t pos,size_t n,void *data) {
    struct mapped_image *m=io->data;
    if(!m->writable || pos<0 || (uint64_t)pos>m->size || n>m->size-(size_t)pos)return 0;
    memcpy(m->base+(size_t)pos,data,n);return 1;
}
static void mapped_close(io_func *io) {
    struct mapped_image *m=io->data;
    if(m->writable && (msync(m->base,m->size,MS_SYNC) || fsync(m->fd)))die("image sync","");
    if(munmap(m->base,m->size) || close(m->fd))die("image close","");
    free(m);free(io);
}
static io_func *mapped_open(const char *path,int writable) {
    int fd=open(path,(writable?O_RDWR:O_RDONLY)|O_NOFOLLOW);struct stat st;
    if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || (uint64_t)st.st_size>SIZE_MAX)
        die("image must be fixed-size regular file",path);
    struct mapped_image *m=calloc(1,sizeof(*m));io_func *io=calloc(1,sizeof(*io));
    if(!m || !io)die("image map allocation",path);
    m->fd=fd;m->size=(size_t)st.st_size;m->writable=writable;
    m->base=mmap(NULL,m->size,PROT_READ|(writable?PROT_WRITE:0),MAP_SHARED,fd,0);
    if(m->base==MAP_FAILED)die("image mmap",path);
    io->data=m;io->read=mapped_read;io->write=mapped_write;io->close=mapped_close;return io;
}
static char *readstr(FILE *f,uint32_t n) {
    if(n>65535)die("manifest string too large","");
    char *p=calloc(1,(size_t)n+1);if(!p || fread(p,1,n,f)!=n)die("manifest short read","");
    if(strlen(p)!=n)die("manifest embedded NUL","");return p;
}
static void copy_data(Volume *v,HFSCatalogNodeID id,const char *src,const char *link,uint32_t mode) {
    HFSPlusCatalogFile *r=(HFSPlusCatalogFile*)getRecordByCNID(id,v);
    if(!r || r->recordType!=kHFSPlusFileRecord)die("missing data record",src);
    if(S_ISLNK(mode)) {
        io_func *io=openRawFile(id,&r->dataFork,(HFSPlusCatalogRecord*)r,v);
        if(!io || !WRITE(io,0,strlen(link),(void*)link))die("symlink data write",src);
        CLOSE(io);
    } else {
        FILE *f=fopen(src,"rb");if(!f)die("source open",src);
        AbstractFile *a=createAbstractFileFromFile(f);writeToHFSFile(r,a,v);a->close(a);
    }
    free(r);
}
static void metadata(Volume *v,HFSPlusCatalogRecord *r,struct input *in) {
    HFSPlusBSDInfo *b;
    if(r->recordType==kHFSPlusFolderRecord) {
        HFSPlusCatalogFolder *d=(HFSPlusCatalogFolder*)r;b=&d->permissions;
        d->createDate=d->contentModDate=d->attributeModDate=in->mtime;d->accessDate=in->atime;
    } else {
        HFSPlusCatalogFile *d=(HFSPlusCatalogFile*)r;b=&d->permissions;
        d->createDate=d->contentModDate=d->attributeModDate=in->mtime;d->accessDate=in->atime;
        if(S_ISLNK(in->mode)){d->userInfo.fileType=kSymLinkFileType;d->userInfo.fileCreator=kSymLinkCreator;}
    }
    b->ownerID=in->uid;b->groupID=in->gid;b->fileMode=in->mode;
    b->adminFlags=0;b->ownerFlags=0;
    if(!updateCatalog(v,r))die("metadata update","");
}
static void verify(Volume *v,HFSPlusCatalogRecord *r,struct input *in,const char *path,const char *src,const char *link) {
    HFSPlusBSDInfo *b;
    if(S_ISDIR(in->mode)) {
        if(r->recordType!=kHFSPlusFolderRecord)die("directory type mismatch",path);
        b=&((HFSPlusCatalogFolder*)r)->permissions;
    } else {
        if(r->recordType!=kHFSPlusFileRecord)die("file type mismatch",path);
        HFSPlusCatalogFile *f=(HFSPlusCatalogFile*)r;b=&f->permissions;
        uint64_t size=S_ISLNK(in->mode)?strlen(link):in->size;
        if(f->dataFork.logicalSize!=size)die("file size mismatch",path);
        io_func *io=openRawFile(f->fileID,&f->dataFork,r,v);if(!io)die("image file open",path);
        FILE *source=S_ISLNK(in->mode)?NULL:fopen(src,"rb");
        if(!source && !S_ISLNK(in->mode))die("verification source open",src);
        unsigned char *a=malloc(1048576),*z=malloc(1048576);
        if(!a || !z)die("verification allocation",path);
        for(uint64_t off=0;off<size;) {
            size_t n=size-off>1048576?1048576:(size_t)(size-off);
            if(!READ(io,off,n,a))die("image read",path);
            if(source){if(fread(z,1,n,source)!=n)die("source read",src);}
            else memcpy(z,link+off,n);
            if(memcmp(a,z,n))die("file contents mismatch",path);
            off+=n;
        }
        free(a);free(z);if(source)fclose(source);CLOSE(io);
    }
    if(b->ownerID!=in->uid || b->groupID!=in->gid || b->fileMode!=in->mode)die("owner/mode mismatch",path);
}
int main(int argc,char **argv) {
    if(argc!=4 || (strcmp(argv[3],"import") && strcmp(argv[3],"verify") && strcmp(argv[3],"resume") && strcmp(argv[3],"repair")))return 2;
    struct stat st;if(stat(argv[1],&st) || !S_ISREG(st.st_mode))die("image must be a regular file",argv[1]);
    int repairing=!strcmp(argv[3],"repair"),resuming=!strcmp(argv[3],"resume"),importing=strcmp(argv[3],"verify")!=0;TestByteOrder();
    io_func *io=mapped_open(argv[1],importing);
    Volume *v=openVolume(io);if(!v)die("volume open",argv[1]);
    FILE *m=fopen(argv[2],"rb");if(!m)die("manifest open",argv[2]);
    char magic[8];if(fread(magic,1,8,m)!=8 || memcmp(magic,"MIXHFS01",8))die("manifest magic",argv[2]);
    struct input in;uint64_t count=0;
    while(1) {
        size_t got=fread(&in,1,sizeof(in),m);if(!got && feof(m))break;
        if(got!=sizeof(in))die("truncated manifest",argv[2]);
        char *path=readstr(m,in.plen),*src=readstr(m,in.slen),*link=readstr(m,in.llen);
        if(path[0]!='/' || (!S_ISDIR(in.mode) && !S_ISREG(in.mode) && !S_ISLNK(in.mode)))die("unsupported entry",path);
        /* Never follow source or image symlinks when obtaining the final record. */
        HFSPlusCatalogRecord *r=getRecordFromPath3(path,v,NULL,NULL,0,0,kHFSRootFolderID);
        if(repairing) {
            if(count || !S_ISREG(in.mode) || !r || r->recordType!=kHFSPlusFileRecord)
                die("repair requires one existing regular file",path);
            HFSPlusCatalogFile *f=(HFSPlusCatalogFile*)r;
            if(f->dataFork.logicalSize!=in.size || f->permissions.ownerID!=in.uid ||
               f->permissions.groupID!=in.gid || f->permissions.fileMode!=in.mode)
                die("repair file identity/size mismatch",path);
            HFSCatalogNodeID id=f->fileID;
            copy_data(v,id,src,link,in.mode);free(r);r=getRecordByCNID(id,v);
            metadata(v,r,&in);verify(v,r,&in,path,src,link);
        } else if(resuming && r) {
            /* A pre-resume fsck checks the tree. Every existing file must
             * match the immutable source before it can be skipped. */
            verify(v,r,&in,path,src,link);
        } else if(importing) {
            if(r && strcmp(path,"/"))die("duplicate image path",path);
            HFSCatalogNodeID id=0;
            if(r)id=((HFSPlusCatalogFolder*)r)->folderID;
            else id=S_ISDIR(in.mode)?newFolder(path,v):newFile(path,v);
            free(r);if(!id)die("create entry",path);
            if(!S_ISDIR(in.mode))copy_data(v,id,src,link,in.mode);
            r=getRecordByCNID(id,v);if(!r)die("created record missing",path);
            metadata(v,r,&in);
        } else {
            if(!r)die("image entry missing",path);
            verify(v,r,&in,path,src,link);
        }
        free(r);free(path);free(src);free(link);count++;
        if(count%5000==0){printf("%s entries=%llu\n",argv[3],(unsigned long long)count);fflush(stdout);}
    }
    fclose(m);closeVolume(v);CLOSE(io);
    printf("%s completed entries=%llu\n",argv[3],(unsigned long long)count);return 0;
}
