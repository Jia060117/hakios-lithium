#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "touch-queue.h"
static void empty(uint8_t *frame) { memset(frame, 0xff, 63); }
static void finger(uint8_t *frame, unsigned i, unsigned id, unsigned event, unsigned x, unsigned y) {
    uint8_t *p = frame + 3 + 6*i;
    p[0] = (uint8_t)((event << 6) | (x >> 8)); p[1] = (uint8_t)x;
    p[2] = (uint8_t)((id << 4) | (y >> 8)); p[3] = (uint8_t)y; p[4] = 0; p[5] = 1;
}
int main(void) {
    struct lithium_touch_geometry g = lithium_touch_geometry_r20();
    assert(g.width == 360 && g.height == 680 && g.right == 359 && g.bottom == 679);
    uint32_t x = 123, y = 456;
    assert(lithium_touch_queue_map(&g,1080,2040,&x,&y) && x == 65535 && y == 65535);
    assert(lithium_touch_queue_map(&g,540,1020,&x,&y) && x == 32768 && y == 32768);
    for (unsigned r = 0; r < 360; r += 90) {
        g.rotation = r; assert(lithium_touch_queue_map(&g,0,0,&x,&y));
        assert(x == ((r == 90 || r == 180) ? 65535u : 0u));
        assert(y == ((r == 180 || r == 270) ? 65535u : 0u));
    }
    g.rotation = 0; g.left = 36; g.right = 323;
    assert(lithium_touch_queue_map(&g,0,0,&x,&y) && x == (36u*65535+179)/359);
    g = (struct lithium_touch_geometry){UINT32_MAX,UINT32_MAX,0,0,UINT32_MAX-1,UINT32_MAX-1,0};
    assert(lithium_touch_queue_map(&g,1080,2040,&x,&y) && x == 65535 && y == 65535);
    g = lithium_touch_geometry_r20();
    struct lithium_touch_queue_state s = {0};
    struct lithium_touch_queue_batch b; uint8_t f[63]; empty(f);
    finger(f,0,14,0,1080,2040);
    assert(lithium_touch_queue_frame(&s,&g,f,63,false,&b));
    assert(b.count == 1 && b.events[0].finger == 0 && b.events[0].type == 1 && b.events[0].x == 65535);
    finger(f,0,14,2,1080,2040);
    assert(lithium_touch_queue_frame(&s,&g,f,63,false,&b) && b.count == 0);
    finger(f,0,14,2,0,0);
    assert(lithium_touch_queue_frame(&s,&g,f,63,false,&b) && b.count == 1 && b.events[0].type == 2);
    struct lithium_touch_queue_state old = s; memset(&b,0x5a,sizeof(b));
    struct lithium_touch_queue_batch prior = b;
    assert(!lithium_touch_queue_frame(&s,&g,f,62,false,&b));
    assert(!memcmp(&s,&old,sizeof(s)) && !memcmp(&b,&prior,sizeof(b)));
    assert(lithium_touch_queue_frame(&s,&g,NULL,0,true,&b) && b.count == 1 && b.events[0].type == 3);
    assert(lithium_touch_queue_frame(&s,&g,NULL,0,true,&b) && b.count == 0);
    empty(f); for(unsigned i=0;i<10;i++) finger(f,i,i+5,0,i*100,i*200);
    assert(lithium_touch_queue_frame(&s,&g,f,63,false,&b) && b.count == 10);
    for(unsigned i=0;i<10;i++) assert(b.events[i].finger == i && b.events[i].type == 1);
    /* IDs reused in a new DOWN: releases precede all replacement DOWNs. */
    assert(lithium_touch_queue_frame(&s,&g,f,63,false,&b) && b.count == 20);
    for(unsigned i=0;i<10;i++) assert(b.events[i].type == 3 && b.events[i+10].type == 1);
    empty(f); assert(lithium_touch_queue_frame(&s,&g,f,63,false,&b) && b.count == 10);
    for(unsigned i=0;i<10;i++) assert(b.events[i].type == 3);
    puts("PASS touch queue adapter: geometry, rotations, 10 slots, ID reuse, loss, atomic reject");
    return 0;
}
