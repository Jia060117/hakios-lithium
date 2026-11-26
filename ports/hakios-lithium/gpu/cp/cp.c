/* SPDX-License-Identifier: GPL-2.0-only
 * Host G4 core; A530 ME_INIT parameters from Linux a5xx_gpu.c:475-518.
 * All transport/address/mapping/lifetime facts are caller-supplied.
 */
#include "cp.h"
#include <limits.h>
#include <string.h>

enum { CP_NOP=0x10, CP_WAIT_MEM_WRITES=0x12, CP_MEM_WRITE=0x3d, CP_ME_INIT=0x48 };
static uint32_t parity(uint32_t v) {
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4;
    return (0x9669u >> (v & 15u)) & 1u;
}
static int pow2(uint32_t x) { return x && !(x & (x-1)); }
static int span(uint64_t total, uint64_t offset, uint64_t bytes) {
    return offset <= total && bytes <= total-offset;
}
static int overlap(uint64_t a, uint64_t na, uint64_t b, uint64_t nb) {
    if (!na || !nb) return 0;
    return a <= b ? b-a < na : a-b < nb;
}
static int cpu_overlap(const void *a, size_t na, const void *b, size_t nb) {
    return overlap((uint64_t)(uintptr_t)a, na, (uint64_t)(uintptr_t)b, nb);
}
static uint32_t le32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24;
}
static void put32(uint8_t *b, uint32_t v) {
    b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24);
}
enum gc_status gc_type4(uint32_t reg, uint32_t count, uint32_t *out) {
    /* Mesa keeps pkt4 payload below 127, rather than silently truncating. */
    if (!out || !reg || reg > 0x3ffffu || !count || count >= 127u) return GC_INVALID;
    *out=0x40000000u | count | parity(count)<<7 | reg<<8 | parity(reg)<<27;
    return GC_OK;
}
enum gc_status gc_type7(uint32_t opcode, uint32_t count, uint32_t *out) {
    if (!out || opcode > 127u || count > 0x3fffu) return GC_INVALID;
    *out=0x70000000u | count | parity(count)<<15 | opcode<<16 | parity(opcode)<<23;
    return GC_OK;
}
enum gc_status gc_decode(uint32_t header, struct gc_packet *out) {
    struct gc_packet p={0,0,0,0}; uint32_t rebuilt;
    if (!out) return GC_INVALID;
    p.type=header>>28;
    if (p.type==4) {
        p.payload=header&127u; p.reg=(header>>8)&0x3ffffu;
        if (gc_type4(p.reg,p.payload,&rebuilt)!=GC_OK || rebuilt!=header) return GC_CORRUPT;
    } else if (p.type==7) {
        p.payload=header&0x3fffu; p.opcode=(header>>16)&127u;
        if (gc_type7(p.opcode,p.payload,&rebuilt)!=GC_OK || rebuilt!=header) return GC_CORRUPT;
    } else return GC_UNSUPPORTED;
    *out=p; return GC_OK;
}
enum gc_status gc_firmware_view(enum gc_fw_kind kind, const void *bytes, size_t size,
                               size_t limit, struct gc_firmware *out) {
    struct gc_firmware v; const uint8_t *b=(const uint8_t *)bytes;
    /* File word zero is opaque: Linux drops it; it is not a size or a magic.
     * Require a nonempty whole-dword payload and enforce caller budget first. */
    if (!out || !b || (kind!=GC_PM4 && kind!=GC_PFP) || size<8 || (size&3u) || size-4>limit) return GC_INVALID;
    v.payload=b+4; v.bytes=size-4; v.file_word0=le32(b); v.whereami_candidate=0;
    if (kind==GC_PFP && v.bytes>=12)
        v.whereami_candidate=((le32(v.payload)&15u)==10u && (le32(v.payload+8)&15u)>=1u);
    *out=v; return GC_OK;
}
enum gc_status gc_me_init_a530(uint32_t *out, size_t capacity, size_t *written) {
    uint32_t packet[9]={0,0x2f,3,0x20000000,0,0,0x0b,0,0};
    if (!out || !written) return GC_INVALID;
    if (capacity<9) return GC_NOSPACE;
    (void)gc_type7(CP_ME_INIT,8,&packet[0]);
    memcpy(out,packet,sizeof(packet)); *written=9; return GC_OK;
}
enum gc_order gc_compare32(uint32_t observed, uint32_t target) {
    uint32_t delta=observed-target;
    if (!delta) return GC_EQUAL;
    if (delta==0x80000000u) return GC_AMBIGUOUS;
    return delta<0x80000000u ? GC_AFTER : GC_BEFORE;
}
enum gc_order gc_compare64(uint64_t observed, uint64_t target) {
    uint64_t delta=observed-target;
    if (!delta) return GC_EQUAL;
    if (delta==UINT64_C(0x8000000000000000)) return GC_AMBIGUOUS;
    return delta<UINT64_C(0x8000000000000000) ? GC_AFTER : GC_BEFORE;
}
enum gc_status gc_ring_free(uint32_t words, uint32_t w, uint32_t r, uint32_t *free_words) {
    if (!free_words || !pow2(words) || words<2 || w>=words || r>=words) return GC_INVALID;
    *free_words=(r-w-1u)&(words-1u); return GC_OK;
}
static int valid_bo(const struct gc_bo *b) {
    return b && b->handle && b->cpu && b->bytes && b->bytes<=SIZE_MAX && b->cpu_bytes>=b->bytes &&
        b->cpu_bytes<=UINTPTR_MAX-(uintptr_t)b->cpu &&
        b->address_space && b->generation && b->pin_token && b->access && !(b->access&~3u) &&
        b->iova<=UINT64_MAX-(b->bytes-1u);
}
static size_t slot_bytes(enum gc_fence_width w) { return w==GC_FENCE64 ? 12u : 4u; }
static enum gc_status fence_range(const struct gc_bo *b, uint64_t at, enum gc_fence_width w) {
    if (!valid_bo(b) || !(b->access&GC_WRITE) || (w!=GC_FENCE32 && w!=GC_FENCE64)) return GC_INVALID;
    if (!span(b->bytes,at,slot_bytes(w)) || ((b->iova+at)&(w==GC_FENCE64 ? 7u : 3u))) return GC_RANGE;
    return GC_OK;
}
static size_t tail(uint64_t address, enum gc_fence_width width, uint32_t seq32, uint64_t seq64,
                   uint32_t words[12]) {
    size_t n=0;
    (void)gc_type7(CP_WAIT_MEM_WRITES,0,&words[n++]);
    (void)gc_type7(CP_MEM_WRITE,width==GC_FENCE64 ? 4 : 3,&words[n++]);
    words[n++]=(uint32_t)address; words[n++]=(uint32_t)(address>>32);
    words[n++]=width==GC_FENCE64 ? (uint32_t)seq64 : seq32;
    if (width==GC_FENCE64) {
        words[n++]=(uint32_t)(seq64>>32);
        (void)gc_type7(CP_WAIT_MEM_WRITES,0,&words[n++]);
        (void)gc_type7(CP_MEM_WRITE,3,&words[n++]);
        words[n++]=(uint32_t)(address+8); words[n++]=(uint32_t)((address+8)>>32);
        words[n++]=seq32;
    }
    (void)gc_type7(CP_WAIT_MEM_WRITES,0,&words[n++]);
    return n;
}
enum gc_status gc_probe_stream(const struct gc_bo *b, uint64_t at, enum gc_fence_width width,
                              uint32_t seq32, uint64_t seq64, uint32_t *out, size_t capacity, size_t *written) {
    uint32_t packet[13]; size_t n; enum gc_status rc;
    if (!out || !written) return GC_INVALID;
    rc=fence_range(b,at,width); if (rc!=GC_OK) return rc;
    n=width==GC_FENCE64 ? 13 : 7; if (capacity<n) return GC_NOSPACE;
    (void)gc_type7(CP_NOP,0,&packet[0]);
    n=1+tail(b->iova+at,width,seq32,seq64,packet+1);
    memcpy(out,packet,n*4); *written=n; return GC_OK;
}
static int bound_bo(const struct gc_context *c, const struct gc_bo *b, uint32_t handle, uint32_t access) {
    return valid_bo(b) && b->handle==handle && (b->access&access)==access &&
        b->address_space==c->cfg.address_space && b->generation==c->cfg.generation &&
        b->iova>=c->cfg.aperture_base && span(c->cfg.aperture_bytes,b->iova-c->cfg.aperture_base,b->bytes);
}
static void release_all(struct gc_context *c) {
    while (c->bo_pins) { --c->bo_pins; c->ops.release(c->ops.cookie,&c->bos[c->bo_pins]); }
    if (c->base_pins==2) { c->ops.release(c->ops.cookie,&c->fence); --c->base_pins; }
    if (c->base_pins) { c->ops.release(c->ops.cookie,&c->ring); --c->base_pins; }
    c->bo_count=0; c->retain_dma=0;
}
static void release_batch(struct gc_context *c) {
    while (c->bo_pins) { --c->bo_pins; c->ops.release(c->ops.cookie,&c->bos[c->bo_pins]); }
    c->bo_count=0;
}
enum gc_status gc_init(struct gc_context *c, const struct gc_config *cfg, const struct gc_ops *ops) {
    uint32_t r; enum gc_status rc=GC_INVALID;
    if (!c || !cfg || !ops || (c->state!=GC_EMPTY && c->state!=GC_STOPPED)) return GC_INVALID;
    if (!ops->resolve || !ops->pin || !ops->release || !ops->to_device || !ops->from_device ||
        !ops->read32 || !ops->read_rptr || !ops->publish_wptr || !ops->now_ns ||
        !ops->request_quiesce || !ops->dma_stopped || !cfg->address_space || !cfg->generation ||
        !cfg->aperture_bytes || cfg->aperture_base>UINT64_MAX-(cfg->aperture_bytes-1u) ||
        cfg->ring_handle==cfg->fence_handle || !pow2(cfg->ring_words) || cfg->ring_words<16 ||
        cfg->ring_words>0x40000u || cfg->initial_wptr>=cfg->ring_words || (cfg->ring_offset&3u) ||
        !cfg->staging || cfg->staging_words<13 || cfg->staging_words>SIZE_MAX/4 ||
        ((uintptr_t)cfg->staging&3u) || cfg->staging_words*4>UINTPTR_MAX-(uintptr_t)cfg->staging ||
        (cfg->fence_width!=GC_FENCE32 && cfg->fence_width!=GC_FENCE64)) return GC_INVALID;
    c->state=GC_BUSY_STATE; c->cfg=*cfg; c->ops=*ops; c->base_pins=0; c->bo_pins=0;
    c->bo_count=0; c->retain_dma=0; c->quiesce_requested=0; c->observed_complete=0;
    if (ops->dma_stopped(ops->cookie)!=1) { rc=GC_RETAIN_DMA; goto fail; }
    if (ops->resolve(ops->cookie,cfg->ring_handle,GC_READ,&c->ring) ||
        !bound_bo(c,&c->ring,cfg->ring_handle,GC_READ) ||
        !span(c->ring.bytes,cfg->ring_offset,(uint64_t)cfg->ring_words*4) ||
        ((c->ring.iova+cfg->ring_offset)&3u)) goto fail;
    if (ops->pin(ops->cookie,&c->ring)) { rc=GC_CALLBACK; goto fail; } c->base_pins=1;
    if (ops->resolve(ops->cookie,cfg->fence_handle,GC_WRITE,&c->fence) ||
        !bound_bo(c,&c->fence,cfg->fence_handle,GC_WRITE) ||
        fence_range(&c->fence,cfg->fence_offset,cfg->fence_width)!=GC_OK ||
        overlap(c->ring.iova,c->ring.bytes,c->fence.iova,c->fence.bytes) ||
        cpu_overlap(c->ring.cpu,c->ring.cpu_bytes,c->fence.cpu,c->fence.cpu_bytes) ||
        c->ring.pin_token==c->fence.pin_token ||
        cpu_overlap(cfg->staging,cfg->staging_words*4,c->ring.cpu,c->ring.cpu_bytes) ||
        cpu_overlap(cfg->staging,cfg->staging_words*4,c->fence.cpu,c->fence.cpu_bytes) ||
        cpu_overlap(cfg->staging,cfg->staging_words*4,c,sizeof(*c))) goto fail;
    if (ops->pin(ops->cookie,&c->fence)) { rc=GC_CALLBACK; goto fail; } c->base_pins=2;
    if (ops->read_rptr(ops->cookie,&r) || r!=cfg->initial_wptr ||
        ops->now_ns(ops->cookie,&c->last_now)) { rc=GC_CALLBACK; goto fail; }
    c->wptr=cfg->initial_wptr; c->seq32=cfg->last_seq32; c->seq64=cfg->last_seq64;
    c->state=GC_IDLE; c->last_error=GC_OK; return GC_OK;
fail:
    release_all(c); c->state=GC_EMPTY; c->last_error=rc; return rc;
}
static enum gc_status user_stream(struct gc_context *c, const uint32_t *words, size_t count) {
    size_t at=0; struct gc_packet p;
    while (at<count) {
        enum gc_status rc=gc_decode(words[at],&p); if (rc!=GC_OK) return rc;
        if (p.type!=7 || p.payload>count-at-1) return GC_CORRUPT;
        if (p.opcode==CP_NOP) { at+=(size_t)p.payload+1; continue; }
        if (p.opcode!=CP_MEM_WRITE) return GC_UNSUPPORTED;
        if (p.payload<3) return GC_CORRUPT;
        {
            uint64_t address=(uint64_t)words[at+1] | (uint64_t)words[at+2]<<32;
            uint64_t bytes=(uint64_t)(p.payload-2)*4; unsigned i; int allowed=0;
            if (address&3u || address>UINT64_MAX-(bytes-1u) ||
                overlap(address,bytes,c->ring.iova,c->ring.bytes) ||
                overlap(address,bytes,c->fence.iova,c->fence.bytes)) return GC_RANGE;
            for (i=0;i<c->bo_count;i++) {
                const struct gc_bo *b=&c->bos[i];
                if ((b->access&GC_WRITE) && address>=b->iova && span(b->bytes,address-b->iova,bytes)) allowed=1;
            }
            if (!allowed) return GC_RANGE;
        }
        at+=(size_t)p.payload+1;
    }
    return at==count ? GC_OK : GC_CORRUPT;
}
static enum gc_status request_stop(struct gc_context *c, enum gc_status reason) {
    int requested=0, proof;
    c->state=GC_BUSY_STATE; c->retain_dma=1; c->last_error=reason;
    if (!c->quiesce_requested) {
        c->quiesce_requested=1;
        requested=c->ops.request_quiesce(c->ops.cookie);
    }
    proof=c->ops.dma_stopped(c->ops.cookie);
    if (!requested && proof==1) {
        release_all(c); c->state=GC_STOPPED;
        return reason==GC_OK ? GC_OK : reason;
    }
    c->state=GC_RETAINED;
    return reason==GC_TIMEOUT ? GC_TIMEOUT : GC_RETAIN_DMA;
}
enum gc_status gc_submit_abi(struct gc_context *c, HakiosGpuSubmit *request,
                            const HakiosGpuSubmitBo *bos, size_t bo_count,
                            const HakiosGpuSubmitCmd *cmds, size_t cmd_count, uint64_t deadline) {
    unsigned i,j; size_t total=0; uint32_t r,free_words,next32; uint64_t now,next64;
    uint32_t end_words[12]; size_t end_count; enum gc_status rc=GC_INVALID;
    if (!c || c->state!=GC_IDLE) return GC_BUSY;
    if (!request || !bos || !cmds || !bo_count || bo_count>GC_MAX_BOS || !cmd_count || cmd_count>GC_MAX_CMDS ||
        bo_count!=request->nr_bos || cmd_count!=request->nr_cmds || request->flags!=HAKIOS_GPU_PIPE_3D ||
        request->queueid!=c->cfg.queue_id || request->pad || request->fence_fd!=-1 ||
        request->nr_in_syncobjs || request->nr_out_syncobjs || request->in_syncobjs || request->out_syncobjs ||
        request->syncobj_stride || cpu_overlap(c->cfg.staging,c->cfg.staging_words*4,request,sizeof(*request)) ||
        cpu_overlap(c->cfg.staging,c->cfg.staging_words*4,bos,bo_count*sizeof(*bos)) ||
        cpu_overlap(c->cfg.staging,c->cfg.staging_words*4,cmds,cmd_count*sizeof(*cmds))) return GC_INVALID;
    c->state=GC_BUSY_STATE;
    if (c->ops.now_ns(c->ops.cookie,&now) || now<c->last_now || deadline<=now) { rc=GC_INVALID; goto fail; }
    c->last_now=now;
    for (i=0;i<bo_count;i++) {
        uint32_t access=bos[i].flags;
        if (!bos[i].handle || !access || (access&~3u) || bos[i].handle==c->cfg.ring_handle || bos[i].handle==c->cfg.fence_handle) goto fail;
        for (j=0;j<i;j++) if (bos[j].handle==bos[i].handle) goto fail;
        if (c->ops.resolve(c->ops.cookie,bos[i].handle,access,&c->bos[i]) || !bound_bo(c,&c->bos[i],bos[i].handle,access)) goto fail;
        /* Enforce exactly the ABI-granted access, not a more permissive BO. */
        c->bos[i].access=access;
        if (overlap(c->bos[i].iova,c->bos[i].bytes,c->ring.iova,c->ring.bytes) ||
            overlap(c->bos[i].iova,c->bos[i].bytes,c->fence.iova,c->fence.bytes) ||
            cpu_overlap(c->bos[i].cpu,c->bos[i].cpu_bytes,c->ring.cpu,c->ring.cpu_bytes) ||
            cpu_overlap(c->bos[i].cpu,c->bos[i].cpu_bytes,c->fence.cpu,c->fence.cpu_bytes) ||
            c->bos[i].pin_token==c->ring.pin_token || c->bos[i].pin_token==c->fence.pin_token ||
            cpu_overlap(c->cfg.staging,c->cfg.staging_words*4,c->bos[i].cpu,c->bos[i].cpu_bytes)) goto fail;
        for (j=0;j<i;j++) if (overlap(c->bos[i].iova,c->bos[i].bytes,c->bos[j].iova,c->bos[j].bytes) ||
            c->bos[i].pin_token==c->bos[j].pin_token ||
            cpu_overlap(c->bos[i].cpu,c->bos[i].cpu_bytes,c->bos[j].cpu,c->bos[j].cpu_bytes)) goto fail;
        if (c->ops.pin(c->ops.cookie,&c->bos[i])) { rc=GC_CALLBACK; goto fail; }
        ++c->bo_pins; ++c->bo_count;
    }
    for (i=0;i<cmd_count;i++) {
        const HakiosGpuSubmitCmd *cmd=&cmds[i]; const struct gc_bo *b;
        rc=GC_INVALID;
        if (cmd->type!=HAKIOS_GPU_SUBMIT_COMMAND_BUFFER || cmd->submit_idx>=bo_count || cmd->pad ||
            cmd->nr_relocs || cmd->relocs || !cmd->size || (cmd->size&3u) || (cmd->submit_offset&3u)) goto fail;
        b=&c->bos[cmd->submit_idx];
        if (!(b->access&GC_READ) || !span(b->bytes,cmd->submit_offset,cmd->size)) { rc=GC_RANGE; goto fail; }
        if (cmd->size/4>c->cfg.staging_words-total) { rc=GC_NOSPACE; goto fail; }
        /* Private immutable snapshot: validate what will actually be emitted. */
        for (j=0;j<cmd->size/4;j++) c->cfg.staging[total+j]=le32(b->cpu+cmd->submit_offset+(size_t)j*4);
        rc=user_stream(c,c->cfg.staging+total,cmd->size/4); if (rc!=GC_OK) goto fail;
        total+=cmd->size/4;
    }
    next32=c->seq32+1u; if (!next32) next32=1; /* legacy HakiOS zero=failure; wire encoder accepts zero */
    next64=c->seq64+1u;
    end_count=tail(c->fence.iova+c->cfg.fence_offset,c->cfg.fence_width,next32,next64,end_words);
    if (end_count>c->cfg.staging_words-total || total+end_count>=c->cfg.ring_words) { rc=GC_NOSPACE; goto fail; }
    if (c->ops.read_rptr(c->ops.cookie,&r) || gc_ring_free(c->cfg.ring_words,c->wptr,r,&free_words)!=GC_OK) { rc=GC_CALLBACK; goto fail; }
    if (total+end_count>free_words) { rc=GC_NOSPACE; goto fail; }
    memcpy(c->cfg.staging+total,end_words,end_count*4); total+=end_count;
    /* Stale marker cannot equal this submission's commit, even after wrap. */
    put32(c->fence.cpu+c->cfg.fence_offset+(c->cfg.fence_width==GC_FENCE64 ? 8 : 0),~next32);
    if (c->ops.to_device(c->ops.cookie,&c->fence,c->cfg.fence_offset,slot_bytes(c->cfg.fence_width))) { rc=GC_CALLBACK; goto fail; }
    for (i=0;i<bo_count;i++) if (c->ops.to_device(c->ops.cookie,&c->bos[i],0,(size_t)c->bos[i].bytes)) { rc=GC_CALLBACK; goto fail; }
    for (i=0;i<total;i++) {
        uint32_t slot=(c->wptr+i)&(c->cfg.ring_words-1u);
        put32(c->ring.cpu+c->cfg.ring_offset+(size_t)slot*4,c->cfg.staging[i]);
    }
    if (c->ops.to_device(c->ops.cookie,&c->ring,c->cfg.ring_offset,(size_t)c->cfg.ring_words*4)) { rc=GC_CALLBACK; goto fail; }
    c->start=c->wptr; c->batch_words=(uint32_t)total;
    c->end=(c->wptr+(uint32_t)total)&(c->cfg.ring_words-1u); c->wptr=c->end;
    c->seq32=next32; c->seq64=next64; c->deadline=deadline;
    c->observed_complete=0; c->retain_dma=1;
    /* BUSY during callback; an ambiguous publication error is a DMA hazard. */
    if (c->ops.publish_wptr(c->ops.cookie,c->end)) return request_stop(c,GC_CALLBACK);
    c->state=GC_INFLIGHT; c->last_error=GC_OK; request->fence=next32; return GC_OK;
fail:
    release_batch(c); c->state=GC_IDLE; c->last_error=rc; return rc;
}
enum gc_status gc_poll(struct gc_context *c) {
    uint64_t now; uint32_t r,commit1,commit2,lo=0,hi=0; int matched;
    uint64_t offset;
    if (!c) return GC_INVALID;
    if (c->state==GC_RETAINED) return GC_RETAIN_DMA;
    if (c->state==GC_COMPLETE) return GC_OK;
    if (c->state!=GC_INFLIGHT) return GC_BUSY;
    c->state=GC_BUSY_STATE;
    if (c->ops.now_ns(c->ops.cookie,&now) || now<c->last_now) return request_stop(c,GC_CALLBACK);
    c->last_now=now;
    /* Completion first observed at/after deadline is not a timely result. */
    if (now>=c->deadline) return request_stop(c,GC_TIMEOUT);
    if (c->ops.read_rptr(c->ops.cookie,&r) || r>=c->cfg.ring_words ||
        ((r-c->start)&(c->cfg.ring_words-1u))>c->batch_words) return request_stop(c,GC_CORRUPT);
    offset=c->cfg.fence_offset;
    if (c->ops.from_device(c->ops.cookie,&c->fence,offset,slot_bytes(c->cfg.fence_width))) return request_stop(c,GC_CALLBACK);
    if (c->cfg.fence_width==GC_FENCE64) offset+=8;
    if (c->ops.read32(c->ops.cookie,&c->fence,offset,&commit1)) return request_stop(c,GC_CALLBACK);
    matched=commit1==c->seq32;
    if (matched && c->cfg.fence_width==GC_FENCE64) {
        if (c->ops.read32(c->ops.cookie,&c->fence,c->cfg.fence_offset,&lo) ||
            c->ops.read32(c->ops.cookie,&c->fence,c->cfg.fence_offset+4,&hi) ||
            c->ops.read32(c->ops.cookie,&c->fence,offset,&commit2)) return request_stop(c,GC_CALLBACK);
        matched=commit1==commit2 && ((uint64_t)lo | (uint64_t)hi<<32)==c->seq64;
    }
    if (matched && r==c->end) {
        c->state=GC_COMPLETE; c->observed_complete=1; return GC_OK;
    }
    c->state=GC_INFLIGHT; return GC_PENDING;
}
enum gc_status gc_quiesce(struct gc_context *c) {
    if (!c) return GC_INVALID;
    if (c->state==GC_STOPPED) return GC_OK;
    if (c->state!=GC_IDLE && c->state!=GC_INFLIGHT && c->state!=GC_COMPLETE && c->state!=GC_RETAINED) return GC_BUSY;
    return request_stop(c,GC_OK);
}
