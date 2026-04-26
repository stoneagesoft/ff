/**
 * @file ff_stack.c
 * @brief Stack init/destroy. The hot push/pop helpers live inline in
 *        ff_stack_p.h; this file only carries the out-of-line bits.
 */

#include "ff_stack_p.h"


/**
 * Reset @p s to an empty stack.
 * @param s Stack to initialize.
 */
void ff_stack_init(ff_stack_t *s)
{
    memset(s, 0, sizeof(*s));
}

/**
 * Tear down a stack. No heap is owned, so this just zeroes the struct
 * for symmetry with init/destroy pairs elsewhere.
 * @param s Stack to destroy.
 */
void ff_stack_destroy(ff_stack_t *s)
{
    memset(s, 0, sizeof(*s));
}
