/* Native lithium diagnostics, derived from ios-sailfish early console. */
#ifndef _ARM64_LITHIUM_EARLY_H_
#define _ARM64_LITHIUM_EARLY_H_
#include <pexpert/arm64/board_config.h>
#include <stdint.h>
#include <stdarg.h>
#if defined(LITHIUM)
void lithium_early_mark(unsigned int stage, const char *label);
void lithium_early_putc(char c);
void lithium_early_panic(const char *fmt, va_list *args);
void lithium_early_relocate(uint64_t base);
#else
static inline void lithium_early_mark(unsigned int stage, const char *label)
{ (void)stage; (void)label; }
#endif
#endif
