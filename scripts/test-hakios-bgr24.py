#!/usr/bin/env python3
"""Exercise the actual adapted HakiOS drawRows and debug blit with guarded buffers."""
from pathlib import Path
import subprocess
import hakios
import hakios_bgr24


def section(text, start):
    at = text.index(start)
    brace = text.index('{', at)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[at:end]


def main():
    hakios.resource_check()
    upstream = hakios.ROOT / 'upstream/hakios-xnu-kernel'
    original = upstream / 'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    source = hakios_bgr24.adapt(original.read_text())
    dest = hakios.workspace(hakios.read(hakios.MANIFEST)) / 'lithium-ui-host-checks'
    dest.mkdir(exist_ok=True)
    harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#include <algorithm>
struct VC { uintptr_t v_baseaddr; uint32_t v_width, v_height, v_depth, v_rowbytes; } vinfo;
struct RowJob;
struct IOMobileFramebuffer {
 uint8_t *fBase; uint32_t fPanelWidth, fPanelHeight, fRowBytes, fPixelBytes;
 uint32_t *fColumn; uint16_t *fColumnWeight; bool fDiff;
 uint64_t fDiffPixels=0; uint32_t fDiffLeft=~0u, fDiffRight=0, fDiffTop=~0u, fDiffBottom=0;
 void drawRows(const RowJob *, int32_t, int32_t, uint32_t *);
};
'''
    harness += '\n' + section(source, 'static inline uint32_t\nblend(')
    harness += '\n' + section(source, 'static inline int64_t\nsample_at(')
    harness += '\n' + section(source, 'struct RowJob {') + ';\n'
    harness += section(source, 'void\nIOMobileFramebuffer::drawRows(')
    harness += '\n' + section(source, 'extern "C" void\nMiniIOMobileFramebufferShow(')
    harness += r'''
static void guards(const std::vector<uint8_t> &v, unsigned stride, unsigned w, unsigned h, unsigned bytes) {
 for (size_t i=0;i<64;i++) assert(v[i]==0xa5);
 for (size_t i=64+stride*h;i<v.size();i++) assert(v[i]==0xa5);
 for (unsigned y=0;y<h;y++) for(unsigned x=w*bytes;x<stride;x++) assert(v[64+y*stride+x]==0xa5);
}
int main() {
 unsigned checks=0;
 for (int mode=0;mode<4;mode++) for(int partial=0;partial<2;partial++) {
  const unsigned w=5,h=4,s32=24,s24=20;
  std::vector<uint8_t> p32(128+s32*h,0xa5),p24(128+s24*h,0xa5);
  std::vector<uint32_t> src(8*8);
  for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++) src[y*8+x]=0xf7000000u | ((y*39+x*17)<<16) | ((x*23+y*11)<<8) | (x*7+y*29);
  uint32_t cols[w]; uint16_t weights[w]; uint32_t mixed32[8],mixed24[8];
  RowJob j{}; j.pixels=(const uint8_t *)src.data(); j.stride=32;
  j.sy=1;j.sx=mode==0?1:0;j.sw=mode==0?3:4;j.sh=mode==0?2:3;
  j.px0=mode==0?1:mode==3?-1:0;j.px1=mode==0?4:mode==3?6:5;
  j.py0=mode==0?1:0;j.py1=mode==0?3:4;
  j.cx0=mode==0?1:0;j.cx1=mode==0?4:5;
  j.filter=mode==2||mode==3;j.unscaled=mode==0;
  for(unsigned x=0;x<w;x++) {
   int64_t at=sample_at((int64_t)x-j.px0,j.sw,j.px1-j.px0);
   cols[x]=j.filter?at>>8:(uint32_t)((int64_t)(x-j.px0)*j.sw/(j.px1-j.px0));
   weights[x]=j.filter&&cols[x]+1<(uint32_t)j.sw?at&255:0;
  }
  IOMobileFramebuffer a{p32.data()+64,w,h,s32,4,cols,weights,true};
  IOMobileFramebuffer b{p24.data()+64,w,h,s24,3,cols,weights,true};
  int y0=mode==0?1:partial?1:-1, y1=mode==0?3:partial?3:5;
  a.drawRows(&j,y0,y1,mixed32);b.drawRows(&j,y0,y1,mixed24);
  for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) for(unsigned c=0;c<3;c++)
   assert(p24[64+y*s24+x*3+c]==p32[64+y*s32+x*4+c]);
  guards(p24,s24,w,h,3);guards(p32,s32,w,h,4);
  if(mode) {assert(b.fDiffPixels>0);assert(a.fDiffPixels==b.fDiffPixels);}
  checks++;
 }
 for(unsigned depth: {24u,32u}) {
  const unsigned w=5,h=3,bytes=depth/8,stride=bytes==3?20:24;
  std::vector<uint8_t> out(128+stride*h,0xa5);
  std::vector<uint8_t> in(7*4*5);
  for(size_t i=0;i<in.size();i++) in[i]=(i*19+7)&255;
  vinfo={(uintptr_t)(out.data()+64),w,h,depth,stride};
  MiniIOMobileFramebufferShow(in.data(),7,5,28);
  for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) for(unsigned c=0;c<bytes;c++)
   assert(out[64+y*stride+x*bytes+c]==in[y*28+x*4+c]);
  guards(out,stride,w,h,bytes);checks++;
  for(int bad=0;bad<4;bad++) {
   std::fill(out.begin(),out.end(),0xa5);
   vinfo={(uintptr_t)(out.data()+64),w,h,bad==0?16u:depth,bad==1?1u:stride};
   MiniIOMobileFramebufferShow(bad==2?nullptr:in.data(),7,5,bad==3?1u:28u);
   for(auto c:out) assert(c==0xa5);checks++;
  }
 }
 printf("%u actual framebuffer cases passed\n",checks);
}
'''
    file = dest / 'framebuffer-check.cpp'
    file.write_text(harness)
    binary = dest / 'framebuffer-check'
    cmd = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
        '-fno-omit-frame-pointer', str(file), '-o', str(binary)]
    subprocess.run(cmd, check=True)
    result = subprocess.check_output([str(binary)], text=True).strip()
    assert result == '18 actual framebuffer cases passed', result
    report = dict(passed=True, cases=18, actual_adapted_drawRows_and_debug_blit_exercised=True,
        sanitizer='ASAN+UBSAN', bgr24_matches_existing_bgra32_RGB=True,
        row_padding_and_surrounding_guards_preserved=True,
        scaled_filtered_clipped_and_unscaled_paths_checked=True,
        invalid_blit_inputs_rejected=True,
        original_source_sha256=hakios.sha(original),
        generator_sha256=hakios.sha(hakios.ROOT / 'scripts/hakios_bgr24.py'),
        adapted_source_sha256=__import__('hashlib').sha256(source.encode()).hexdigest(),
        test_source_sha256=hakios.sha(Path(__file__)),
        hardware_verified=False, full_kernel_compiled=False, qemu_used=False)
    hakios.write(hakios.ROOT / 'reports/hakios-bgr24-host-checks.json', report)
    print(result)


if __name__ == '__main__':
    main()
