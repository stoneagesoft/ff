/**
 * @file ff_stack_p.h
 * @brief Fixed-capacity LIFO of @ref ff_int_t cells.
 *
 * Used for both the data stack (@ref ff::stack) and the return stack
 * (@ref ff::r_stack). Capacity is set at compile time via
 * @ref FF_STACK_SIZE; overflow/underflow are caught at the case-body
 * level by the @c FF_SL / @c FF_SO macros, not here.
 *
 * Hot-path access uses the inline @ref ff_tos / @ref ff_nos pointer
 * accessors; the inner interpreter additionally caches the top cell
 * in a local register (see @ref ff.c::ff_exec).
 */

#pragma once

#include <ff_config_p.h>
#include <ff_types_p.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>


typedef struct ff_stack ff_stack_t;

/**
 * Initialize a stack to empty.
 * @param s Stack to initialize.
 */
void ff_stack_init(ff_stack_t *s);

/**
 * Tear down a stack. No-op today; provided for symmetry.
 * @param s Stack to destroy.
 */
void ff_stack_destroy(ff_stack_t *s);


/**
 * @struct ff_stack
 * @brief Fixed-capacity stack of @ref ff_int_t cells.
 *
 * Stack grows toward higher indices; @ref top is the count of valid
 * entries (also = the next-free-slot index).
 */
struct ff_stack
{
    ff_int_t data[FF_STACK_SIZE]; /**< Inline storage. */
    size_t top;                   /**< Count of valid entries. */
};


/* Hot path — inline to avoid cross-TU function-call overhead. */

/**
 * @param s Stack.
 * @return Pointer to the top-of-stack cell. UB if empty.
 */
static inline ff_int_t *ff_tos(ff_stack_t *s)
{
    return &s->data[s->top - 1];
}

/**
 * @param s Stack.
 * @return Pointer to the next-of-stack cell (one below TOS). UB if depth < 2.
 */
static inline ff_int_t *ff_nos(ff_stack_t *s)
{
    return &s->data[s->top - 2];
}

/**
 * @param s Stack.
 * @param i Depth (0 = TOS, 1 = NOS, …).
 * @return Pointer to the cell at depth @p i. UB if depth ≤ i.
 */
static inline ff_int_t *ff_sat(ff_stack_t *s, size_t i)
{
    return &s->data[s->top - 1 - i];
}

/**
 * Push @p v onto the stack.
 * @param s Stack.
 * @param v Value to push. @ref top must be < @ref FF_STACK_SIZE.
 */
static inline void ff_stack_push(ff_stack_t *s, ff_int_t v)
{
    assert(s->top < FF_STACK_SIZE);
    s->data[s->top++] = v;
}

/**
 * Push a pointer cast to a cell.
 * @param s Stack.
 * @param p Pointer; reinterpret-cast through @c intptr_t.
 */
static inline void ff_stack_push_ptr(ff_stack_t *s, const void *p)
{
    ff_stack_push(s, (ff_int_t)(intptr_t)p);
}

/**
 * Push a real value, storing its bit-pattern in a single cell.
 * @param s Stack.
 * @param r Real value.
 */
static inline void ff_stack_push_real(ff_stack_t *s, ff_real_t r)
{
    ff_int_t v;
    memcpy(&v, &r, sizeof(v));
    ff_stack_push(s, v);
}

/**
 * Pop and return the top of stack.
 * @param s Stack. Must be non-empty.
 * @return Popped value.
 */
static inline ff_int_t ff_stack_pop(ff_stack_t *s)
{
    assert(s->top > 0);
    return s->data[--s->top];
}

/**
 * Drop @p n top cells.
 * @param s Stack. Must hold ≥ @p n items.
 * @param n Number of cells to drop.
 */
static inline void ff_stack_popn(ff_stack_t *s, size_t n)
{
    assert(s->top >= n);
    s->top -= n;
}
