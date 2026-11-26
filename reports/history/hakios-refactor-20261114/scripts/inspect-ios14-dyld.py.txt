#!/usr/bin/env python3
"""Read the firmware dyld from the regular HFS image, verify it, and disassemble.

Run in WSL. The input mapping is read-only and any attempted write aborts.
No phone or host block device is opened.
"""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORK = Path.home() / 'ios-mix1-build'


def main():
    out = WORK / 'dyld-inspection'
    out.mkdir(exist_ok=True)
    host = WORK / 'hfs-host'
    importer = ROOT / 'ports/lithium/host/hfs-root-copy.c'
    source = importer.read_text().split('static char *readstr(', 1)[0]
    source = source.replace('if(!m->writable ||', 'if(!m->writable)abort();\n    if(')
    source += r'''
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    TestByteOrder();io_func *disk=mapped_open(argv[1],0);
    Volume *v=openVolume(disk);if(!v)return 3;
    HFSPlusCatalogRecord *r=getRecordFromPath3(argv[2],v,NULL,NULL,0,0,kHFSRootFolderID);
    if(!r || r->recordType!=kHFSPlusFileRecord)return 4;
    HFSPlusCatalogFile *f=(HFSPlusCatalogFile*)r;
    if(f->dataFork.logicalSize>16*1024*1024)return 5;
    io_func *io=openRawFile(f->fileID,&f->dataFork,r,v);if(!io)return 6;
    FILE *dst=fopen(argv[3],"wb");if(!dst)return 7;
    unsigned char buffer[65536];
    for(uint64_t off=0;off<f->dataFork.logicalSize;) {
        size_t n=f->dataFork.logicalSize-off;
        if(n>sizeof(buffer))n=sizeof(buffer);
        if(!READ(io,off,n,buffer) || fwrite(buffer,1,n,dst)!=n)return 8;
        off+=n;
    }
    if(fclose(dst))return 9;
    CLOSE(io);free(r);closeVolume(v);CLOSE(disk);return 0;
}
'''
    cfile = out / 'read-file.c'
    cfile.write_text(source)
    reader = out / 'read-file'
    libraries = [host / 'build/hfs/libhfs.a', host / 'build/common/libcommon.a']
    subprocess.run(['clang', '-O2', '-I' + str(host / 'includes'), cfile,
                    *libraries, '-lz', '-o', reader], check=True)
    root = json.loads((ROOT / 'reports/ios14-hfs-root.json').read_text())
    files = json.loads((ROOT / 'reports/ios14-root-files.json').read_text())
    expected = next(f for f in files['files'] if f['path'] == '/usr/lib/dyld')
    dyld = out / 'dyld'
    subprocess.run([reader, root['image'], expected['path'], dyld], check=True)
    data = dyld.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert len(data) == expected['bytes'] and digest == expected['sha256']
    for filename, command in (
        ('symbols.txt', ['llvm-nm', '-nm', str(dyld)]),
        ('disassembly.txt', ['llvm-objdump', '--macho', '--disassemble', str(dyld)]),
        ('strings.txt', ['strings', '-a', str(dyld)])):
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        (out / filename).write_text(result.stdout)
    print(json.dumps(dict(binary=str(dyld), sha256=digest, bytes=len(data),
        output=str(out), host_image_read_only=True, device_operations=False)))


if __name__ == '__main__':
    main()
