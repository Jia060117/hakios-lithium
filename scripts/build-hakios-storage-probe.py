#!/usr/bin/env python3
"""Build a tiny APFS test root using HakiOS's own tool and verified 18H17 expectations."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import hakios

def verify_startup(program, output, work):
    """Check the linked Mach-O entry, not just the presence of startup source."""
    blob = program.read_bytes()
    hakios.require(struct.unpack_from('<I', blob)[0] == 0xfeedfacf, 'Expected ARM64 Mach-O probe')
    cursor = 32
    entry = None
    for _ in range(struct.unpack_from('<I', blob, 16)[0]):
        command, size = struct.unpack_from('<II', blob, cursor)
        hakios.require(size >= 8 and cursor + size <= len(blob), 'Invalid probe load command')
        if command == 5:  # LC_UNIXTHREAD, ARM_THREAD_STATE64
            hakios.require(struct.unpack_from('<II', blob, cursor + 8) == (6, 68), 'Unexpected ARM64 thread flavor')
            entry = struct.unpack_from('<Q', blob, cursor + 272)[0]
        cursor += size
    hakios.require(entry is not None, 'Static probe has no LC_UNIXTHREAD entry')
    symbols = subprocess.check_output(['/usr/bin/llvm-nm', '--defined-only', '--format=posix', str(program)], text=True)
    addresses = {line.split()[0]: int(line.split()[2], 16) for line in symbols.splitlines() if len(line.split()) >= 3}
    hakios.require(entry == addresses.get('_start') and '_main' in addresses and '___mini_start' in addresses,
        'Probe must enter the upstream assembly startup and link the complete libmini main path')
    disassembly = subprocess.check_output(['/usr/bin/llvm-objdump', '--disassemble', '--no-show-raw-insn',
        '--start-address=' + hex(entry), '--stop-address=' + hex(entry + 24), str(program)], text=True)
    instructions = [line.split(':', 1)[1].strip() for line in disassembly.splitlines()
        if ':' in line and line.split(':', 1)[0].strip() in {f'{entry + i * 4:x}' for i in range(6)}]
    hakios.require(len(instructions) == 6 and instructions[0] == 'mov\tx0, sp' and
        instructions[1] in ('and\tsp, x0, #0xfffffffffffffff0', 'and\tsp, x0, #0xfffffffffffffff0 ; =-16') and
        instructions[2].startswith('mov\tx29, #0x0') and instructions[3].startswith('mov\tx30, #0x0') and
        instructions[4].startswith('bl\t') and '___mini_start' in instructions[4] and
        instructions[5].startswith('brk\t#0x1'), 'Linked probe startup does not align SP before the C call')
    target = addresses['___mini_start']
    branch = __import__('re').search(r'bl\t0x([0-9a-f]+)', instructions[4])
    hakios.require(branch is not None and int(branch[1], 16) == target, 'Assembly startup branches to an unexpected C runtime')
    report = output / 'startup-disassembly.txt'
    report.write_text(disassembly, encoding='utf-8', newline='\n')
    return dict(entry=hex(entry), c_main=hex(addresses['_main']), c_runtime=hex(target),
        entry_symbol='_start', upstream_assembly_aligns_sp_before_C=True, original_argc_pointer_preserved=True,
        complete_upstream_libmini_linked=True, disassembly_sha256=hakios.sha(report), hardware_verified=False)

def fnv(data):
    value=0xcbf29ce484222325
    for byte in data: value=((value^byte)*0x100000001b3)&((1<<64)-1)
    return value

def verify_syscall_contract(program, output):
    """Verify the final diagnostic calls the linked upstream assembly entry."""
    disassembly = subprocess.check_output(['/usr/bin/llvm-objdump', '-d', '--no-show-raw-insn', str(program)], text=True)
    import re
    functions = dict(re.findall(r'^([0-9a-f]+) <([^>]+)>:', disassembly, re.M))
    addresses = {name:int(address,16) for address,name in functions.items()}
    hakios.require('_mini_syscall' in addresses and '_check_file' in addresses, 'Missing linked syscall or file check')
    check = disassembly.split('<_check_file>:',1)[1].split('\n\n',1)[0]
    hakios.require('svc\t' not in check and '<_mini_syscall>' in check,
        'File checker must use upstream C-call syscall boundary, without inline svc')
    callsite = disassembly.split('<_mini_syscall>:',1)[1].split('\n\n',1)[0]
    hakios.require('mov\tx16, x0' in callsite and 'mov\tx1, x2' in callsite and 'svc\t#0x80' in callsite,
        'Linked syscall entry differs from upstream register argument contract')
    svc_in_diagnostic = []
    current = ''
    for line in disassembly.splitlines():
        match = re.match(r'^[0-9a-f]+ <([^>]+)>:',line)
        if match: current=match[1]
        if 'svc\t' in line and current not in ('_mini_syscall','_fork','_pipe'):
            svc_in_diagnostic.append(current)
    hakios.require(not svc_in_diagnostic, 'Unexpected raw syscall outside pinned upstream assembly')
    path=output/'syscall-contract-disassembly.txt'
    path.write_text(disassembly,encoding='utf-8',newline='\n')
    return dict(complete_upstream_mini_syscall_used=True, inline_svc_in_diagnostic=False,
        file_checker_calls_mini_syscall=True, negative_errno_boundary=True,
        disassembly_sha256=hakios.sha(path), hardware_verified=False)

def main():
    spec=hakios.read(hakios.MANIFEST)
    hakios.policy(spec); hakios.resource_check()
    subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-storage.py')],check=True)
    work=hakios.verify_workspace(spec)
    subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-storage-probe.py')],check=True)
    output=work/'lithium-storage-test'
    output.mkdir(exist_ok=True)
    root=output/'root'
    for name in ('dev','mnt','sbin'): (root/name).mkdir(parents=True,exist_ok=True)
    boundary=hakios.read(hakios.ROOT/'reports/hakios-ios14-boundary.json')
    iosroot=Path('/home/jia/ios-mix1-build/hakios-ios14-root-ro/root')
    paths={'VERSION':'System/Library/CoreServices/SystemVersion.plist','DYLD':'usr/lib/dyld','LAUNCHD':'sbin/launchd'}
    expected={}; lines=[]
    for label,name in paths.items():
        data=(iosroot/name).read_bytes()
        digest=hashlib.sha256(data).hexdigest()
        if label!='VERSION':
            hakios.require(digest==boundary['files'][name]['sha256'],'18H17 input differs: '+name)
        else:
            version=__import__('plistlib').loads(data)
            hakios.require(version['ProductVersion']=='14.8' and version['ProductBuildVersion']=='18H17','Wrong userspace')
        expected[name]=dict(bytes=len(data),sha256=digest,fnv64=f'{fnv(data):016x}')
        lines += [f'#define EXPECTED_{label}_BYTES {len(data)}UL',f'#define EXPECTED_{label}_FNV 0x{fnv(data):016x}UL']
    image=Path('/home/jia/ios-mix1-build/ios14-root-hfs.img')
    with image.open('rb') as stream: header=stream.read(8192)
    hakios.require(image.stat().st_size==17179869184 and header[1024:1026]==b'HX','Installed HFSX input missing')
    lines.append(f'#define EXPECTED_HEADER_FNV 0x{fnv(header):016x}UL')
    (output/'storage_expected.h').write_text('\n'.join(lines)+'\n')
    source=hakios.ROOT/spec['port_path']/'tests/storage_probe.c'
    env=dict(os.environ,THEOS=str(work/'xnu-deps/theos-linux'))
    # Follow userland/build.sh's complete libmini compile/link flow. A C entry
    # cannot assume the kernel's argc stack is already AAPCS64 aligned (R15).
    upstream=hakios.ROOT/spec['baseline']['path']
    runtime_files=['userland/libmini/mini.c', 'userland/libmini/mini.h', 'userland/libmini/start.S']
    runtime_sources={}
    for name in runtime_files:
        pinned=subprocess.check_output(['git','-C',str(upstream),'show',hakios.PIN+':'+name])
        hakios.require((work/name).read_bytes()==pinned, 'Complete upstream libmini source differs: '+name)
        runtime_sources[name]=hashlib.sha256(pinned).hexdigest()
    cc=['bash','-c','. ./xnu-env.sh\nexec xnu-clang "$@"','storage-build',
        '-arch','arm64','-Os','-static','-nostdlib','-ffreestanding','-fno-stack-protector','-fno-builtin']
    commands=[cc+['-I','userland/libmini','-c','userland/libmini/mini.c','-o',str(output/'mini.o')],
        cc+['-c','userland/libmini/start.S','-o',str(output/'start.o')],
        cc+['-Wl,-e,_start','-Wl,-pagezero_size,0x100000000','-I','lithium-storage-test',
            '-o',str(root/'sbin/launchd'),str(source),str(output/'mini.o'),str(output/'start.o')]]
    for command in commands: subprocess.run(command,cwd=work,env=env,check=True)
    startup=verify_startup(root/'sbin/launchd',output,work)
    syscall_contract=verify_syscall_contract(root/'sbin/launchd',output)
    # New storage test roots use exactly the upstream APFS builder and readers.
    subprocess.run(['python3','tools/apfs/mkapfs.py','-o',str(output/'root.apfs'),'Root='+str(root)],cwd=work,check=True)
    cache=hakios.ROOT/'artifacts/hakios/sources/apfsprogs'
    pin='1bd1c1c2140d13d1b6d2e03379316572bee70aa3'
    hakios.require(hakios.git(cache,'rev-parse','HEAD')==pin,'Pinned APFS checker cache differs')
    checker=work/'xnu-deps/build/apfsprogs'
    if not checker.exists():
        subprocess.run(['git','clone','--no-hardlinks',str(cache),str(checker)],check=True)
        subprocess.run(['git','-C',str(checker),'checkout','--detach',pin],check=True)
    hakios.require(hakios.git(checker,'rev-parse','HEAD')==pin,'APFS checker pin differs')
    # Build the reader from the actual prepared HakiOS source. An interrupted WSL
    # session can leave an executable with a fresh timestamp but empty contents.
    subprocess.run(['bash','tools/apfs/build-tool.sh'],cwd=work,
        env=dict(env,MAKEFLAGS='-j'+hakios.build_jobs(),CMAKE_BUILD_PARALLEL_LEVEL=hakios.build_jobs()),check=True)
    subprocess.run(['bash','tools/apfs/check-image.sh',str(output/'root.apfs'),'Root='+str(root)],cwd=work,
        env=dict(env,MAKEFLAGS='-j'+hakios.build_jobs(),CMAKE_BUILD_PARALLEL_LEVEL=hakios.build_jobs()),check=True)
    dest=hakios.ROOT/'artifacts/hakios/lithium'
    shutil.copyfile(output/'root.apfs',dest/'storage-root.apfs')
    shutil.copyfile(root/'sbin/launchd',dest/'storage-probe')
    shutil.copyfile(output/'storage_expected.h',dest/'storage_expected.h')
    shutil.copyfile(output/'startup-disassembly.txt',dest/'storage-startup-disassembly.txt')
    shutil.copyfile(output/'syscall-contract-disassembly.txt',dest/'storage-syscall-disassembly.txt')
    hakios.write(hakios.ROOT/'reports/hakios-storage-probe-build.json',dict(inputs=hakios.inputs(spec),
        probe_sha256=hakios.sha(root/'sbin/launchd'),root_sha256=hakios.sha(output/'root.apfs'),
        root_bytes=(output/'root.apfs').stat().st_size,source_sha256=hakios.sha(source),expected=expected,
        header_fnv64=f'{fnv(header):016x}',header_sha256=hashlib.sha256(header).hexdigest(),
        expected_header_sha256=hakios.sha(output/'storage_expected.h'),apfs_host_readers_passed=True,
        apfsprogs_commit=pin,apfsck_sha256=hakios.sha(checker/'apfsck/apfsck'),
        apfs_tool_sha256=hakios.sha(work/'apfs-work/apfs-tool'),
        startup_contract=startup, syscall_contract=syscall_contract, upstream_runtime_sources=runtime_sources,
        upstream_build_contract_sha256=hakios.sha(work/'userland/build.sh'), build_commands=commands,
        uses_upstream_hfs=True,uses_upstream_ufs_bridge=True,hardware_verified=False,ios14_processes_executed=False,qemu_used=False))
    print('Built and read back HakiOS APFS storage-test root with exact 18H17 file expectations')

if __name__=='__main__': main()
