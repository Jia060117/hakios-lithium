/*
 * Native MIX bootstrap console. Adapted from joranv1/ios-sailfish
 * 6f27e41ff249f2680540ebe8ee5ab982c5e641c9, patches 0010/0012/0079.
 * MIX display capture: BGR888, 1080x2040, stride 3240, PA 0x83401000.
 * No allocation, current thread, locks, firmware or serial hardware needed.
 * start.s and arm_vm_init keep a Normal-NC alias alive across TTBR0 teardown.
 */
#include <arm64/lithium_early.h>
#if defined(LITHIUM)
#include <kern/misc_protos.h>
#define WIDTH 1080u
#define HEIGHT 2040u
#define STRIDE 3240u
#define SCALE 2u
#define CELL_W 16u
#define CELL_H 32u
#define ROWS ((HEIGHT - 32u) / CELL_H)
#define COLS (WIDTH / CELL_W)
extern unsigned char iso_font[256 * 16];
static volatile unsigned char *fb = (volatile unsigned char *)(uintptr_t)0x83401000;
static unsigned int row = 4, col, started, panicking;

static void pixel(unsigned int x, unsigned int y, uint32_t rgb)
{
    volatile unsigned char *p = fb + y * STRIDE + x * 3u;
    p[0] = rgb; p[1] = rgb >> 8; p[2] = rgb >> 16;
}
static void clear_row(void)
{
    for (unsigned int y = row * CELL_H; y < (row + 1) * CELL_H; y++)
        for (unsigned int x = 0; x < WIDTH; x++) pixel(x, y, 0);
}
void lithium_early_putc(char c)
{
    if (!started) { started = 1; clear_row(); }
    if (c == '\r') { col = 0; return; }
    if (c == '\n' || col == COLS) {
        col = 0; row = (row + 1) % ROWS; clear_row();
        __asm__ volatile("dsb sy" ::: "memory");
        if (c == '\n') return;
    }
    const unsigned char *glyph = iso_font + (unsigned char)c * 16;
    for (unsigned int gy = 0; gy < 16; gy++)
        for (unsigned int sy = 0; sy < SCALE; sy++)
            for (unsigned int gx = 0; gx < 8; gx++)
                for (unsigned int sx = 0; sx < SCALE; sx++)
                    pixel(col * CELL_W + gx * SCALE + sx,
                        row * CELL_H + gy * SCALE + sy,
                        (glyph[gy] & (1u << gx)) ? 0xffffffu : 0);
    col++;
}
static void string(const char *s)
{ while (*s) lithium_early_putc(*s++); }
void lithium_early_mark(unsigned int stage, const char *label)
{
    if (stage > 20) return;
    for (unsigned int y = HEIGHT - 32; y < HEIGHT; y++)
        for (unsigned int x = stage * 48; x < (stage + 1) * 48; x++)
            pixel(x, y, 0x00ff00u | (stage * 12u << 16));
    string("XNU LITHIUM ");
    lithium_early_putc('0' + stage / 10);
    lithium_early_putc('0' + stage % 10);
    string(": "); string(label); lithium_early_putc('\n');
}
void lithium_early_relocate(uint64_t base)
{
    fb = (volatile unsigned char *)(uintptr_t)base;
    lithium_early_mark(9, "final framebuffer alias live");
}
void lithium_early_panic(const char *fmt, va_list *args)
{
    if (panicking) return;
    panicking = 1;
    string("\nXNU LITHIUM PANIC: ");
    va_list copy; va_copy(copy, *args);
    _doprnt(fmt, &copy, lithium_early_putc, 16);
    va_end(copy); lithium_early_putc('\n');
    __asm__ volatile("dsb sy" ::: "memory");
}
#endif
