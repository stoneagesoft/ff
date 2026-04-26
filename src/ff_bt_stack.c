/**
 * @file ff_bt_stack.c
 * @brief Back-trace stack init/destroy. The hot push helper is inline
 *        in ff_bt_stack_p.h; only the lifecycle bits live here.
 */

#include "ff_bt_stack_p.h"

#include <string.h>


/**
 * Reset @p bt to an empty stack.
 * @param bt Back-trace stack to initialize.
 */
void ff_bt_stack_init(ff_bt_stack_t *bt)
{
    memset(bt, 0, sizeof(*bt));
}

/**
 * Tear down a back-trace stack. No heap allocated; provided for
 * symmetry.
 * @param bt Stack to destroy.
 */
void ff_bt_stack_destroy(ff_bt_stack_t *bt)
{
    memset(bt, 0, sizeof(*bt));
}
