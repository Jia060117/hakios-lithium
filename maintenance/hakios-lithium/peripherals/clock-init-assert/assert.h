/* Target-suite-only adapter. The retained test source is never rewritten.
 * Only its CFG=1 QUP-entry assertion can accept CFG=0, and only while the
 * explicit initializer is under test. Every other assertion remains strict. */
#pragma GCC system_header
#include_next <assert.h>

#if !defined(R26_STAGE1_ASSERT_ADAPTER)
#error "This assert adapter is only for touch-clock-init-test.c"
#endif

static void r26_assert_check(int passed, const char *expression,
    const char *file, unsigned line);

#undef assert
#define assert(expression) \
    r26_assert_check(!!(expression), #expression, __FILE__, __LINE__)
