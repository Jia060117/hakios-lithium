#include "smmu.h"
static_assert(sizeof(uint64_t)==8,"64bit PTE");
static_assert(alignof(gs_arena)==4096,"pinned table alignment");
static_assert(sizeof(gs_arena)==24*4096,"bounded table arena");
static_assert(GS_POOL_BYTES==8192ull*4096,"fixed pool");
extern "C" gs_status g3_cpp_link_probe(gs_domain *d) { return gs_map_pool(d,GS_READ|GS_WRITE); }
extern "C" gs_status g3_offline_image_probe(gs_domain *d,gs_arena *arena,const gs_config *cfg) {
    gs_status rc=gs_build_pool(d,arena,cfg,GS_READ|GS_WRITE);
    if(rc!=GS_OK)return rc;
    struct gs_table_view view{};
    return gs_table_view(d,0,&view);
}
extern "C" gs_status g3_callback_bind_probe(gs_domain *d,const gs_callbacks *cb) {
    return gs_bind_callbacks(d,cb);
}
