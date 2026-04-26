/**
 * @file ff_bt_stack_p.h
 * @brief Bounded back-trace stack of word pointers.
 *
 * When @ref FF_STATE_BACKTRACE is set, every NEST/DOES_RUNTIME entry
 * pushes the caller word onto this stack. On error the host can walk
 * it from oldest to newest to print a Forth call chain. The stack
 * silently drops pushes once full, so deep recursion doesn't crash —
 * the trailing tail is preserved.
 */

#pragma once

#include <ff_config_p.h>


typedef struct ff_word ff_word_t;
typedef struct ff_bt_stack ff_bt_stack_t;

/**
 * Initialize a back-trace stack to empty.
 * @param bt Stack to initialize.
 */
void ff_bt_stack_init(ff_bt_stack_t *bt);

/**
 * Tear down a back-trace stack. Currently a no-op (storage is inline)
 * but called for symmetry and forward-compatibility.
 * @param bt Stack to destroy.
 */
void ff_bt_stack_destroy(ff_bt_stack_t *bt);


/**
 * @struct ff_bt_stack
 * @brief Fixed-capacity stack of caller word pointers.
 */
struct ff_bt_stack
{
    const ff_word_t *data[FF_BT_STACK_SIZE]; /**< Inline storage; oldest at index 0. */
    int top;                                  /**< Number of valid entries (0..FF_BT_STACK_SIZE). */
};


/**
 * Push a word onto the back-trace stack. Hot path — inlined to avoid
 * a cross-TU call on every NEST when tracing is on. Silently no-ops
 * once the bounded buffer is full.
 *
 * @param s Back-trace stack.
 * @param w Word to record (typically the *caller* at NEST time, not
 *          the callee — see ff_words_ctrl_p.h for the conventions).
 */
static inline void ff_bt_stack_push(ff_bt_stack_t *s, const ff_word_t *w)
{
    if (s->top < FF_BT_STACK_SIZE)
        s->data[s->top++] = w;
}
