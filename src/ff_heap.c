/**
 * @file ff_heap.c
 * @brief Per-word growable cell array used for compiled bytecode and
 *        runtime data, plus a peephole optimizer that fires during
 *        opcode emission.
 */

#include "ff_heap_p.h"

#include "ff_word_p.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>


// Public

/** @copydoc ff_heap_init */

void ff_heap_init(ff_heap_t *h)
{
    memset(h, 0, sizeof(*h));
    h->last_op = FF_OP_NONE;
}

/** @copydoc ff_heap_destroy */
void ff_heap_destroy(ff_heap_t *h)
{
    free(h->data);
    memset(h, 0, sizeof(*h));
}

/** @copydoc ff_heap_align */
void ff_heap_align(ff_heap_t *h)
{
    h->byte_off = 0;
}

/** @copydoc ff_heap_alloc */
void ff_heap_alloc(ff_heap_t *h, size_t cells)
{
    assert(cells);

    ff_heap_ensure(h, cells);
    memset(&h->data[h->size], 0, cells * sizeof(ff_int_t));
    h->size += cells;
    h->byte_off = 0;
    h->last_op = FF_OP_NONE;
}

/** @copydoc ff_heap_compile_word */
void ff_heap_compile_word(ff_heap_t *h, const ff_word_t *w)
{
    assert(w);
    ff_heap_align(h);

    /* Opcodes that need a word pointer to access per-word data at runtime
       (heap.data, does pointer, etc.). */
    if (w->opcode == FF_OP_NEST
            || w->opcode == FF_OP_DOES_RUNTIME
            || w->opcode == FF_OP_CREATE_RUNTIME
            || w->opcode == FF_OP_CONSTANT_RUNTIME
            || w->opcode == FF_OP_ARRAY_RUNTIME
            || w->opcode == FF_OP_DEFER_RUNTIME)
    {
        ff_heap_push(h, w->opcode);
        ff_heap_push(h, (ff_int_t)(intptr_t)w);
    }
    else if (w->opcode >= 0)
    {
        /* Regular opcoded built-in: single-cell opcode. */
        ff_heap_push(h, w->opcode);
    }
    else if (!ff_word_is_native(w))
    {
        /* Non-native without an opcode (legacy colon-def path): nest. */
        ff_heap_push(h, FF_OP_NEST);
        ff_heap_push(h, (ff_int_t)(intptr_t)w);
    }
    else
    {
        /* Native without opcode: external escape hatch. The fn pointer
           is at w->heap.data[0] (see ff_word_native_fn). */
        ff_heap_push(h, FF_OP_CALL);
        ff_heap_push(h, (ff_int_t)(intptr_t)ff_word_native_fn(w));
    }
    h->last_op = FF_OP_NONE;
}

/** @copydoc ff_heap_compile_int */
void ff_heap_compile_int(ff_heap_t *h, ff_int_t v)
{
    ff_heap_align(h);
    ff_heap_push(h, v);
    h->last_op = FF_OP_NONE;
}

/** @copydoc ff_heap_compile_real */
void ff_heap_compile_real(ff_heap_t *h, ff_real_t r)
{
    ff_int_t v;
    memcpy(&v, &r, sizeof(v));
    ff_heap_align(h);
    ff_heap_push(h, v);
    h->last_op = FF_OP_NONE;
}

/** @copydoc ff_heap_compile_char */
void ff_heap_compile_char(ff_heap_t *h, char c)
{
    if (!h->byte_off)
    {
        ff_heap_ensure(h, 1);
        h->data[h->size++] = 0;
    }
    *((char *)(&h->data[h->size - 1]) + h->byte_off) = c;
    h->byte_off = (h->byte_off + 1) % sizeof(ff_int_t);
    h->last_op = FF_OP_NONE;
}

/** @copydoc ff_heap_compile_str */
void ff_heap_compile_str(ff_heap_t *h, const char *s, size_t len)
{
    ff_heap_align(h);

    int cells = (len + 1 + sizeof(ff_int_t)) / sizeof(ff_int_t);
    ff_heap_push(h, cells + 1); /* skip length */
    ff_heap_ensure(h, cells);
    memset(&h->data[h->size], 0, cells * sizeof(ff_int_t));
    memcpy(&h->data[h->size], s, len);
    h->size += cells;
    h->last_op = FF_OP_NONE;
}

/**
 * Try to fold @c (h->last_op, op) into a single specialized op or
 * superinstruction. The caller skips the regular emit when this
 * returns true.
 *
 * Only literal-class opcodes (LIT0 / LIT1 / LITM1 / LIT) are
 * considered as the "left" side; @ref ff_heap_compile_int and friends
 * clear @ref ff_heap::last_op so unrelated tail cells (e.g.
 * branch-offset placeholders) are never mistaken for a folded LIT.
 *
 * @param h  Heap.
 * @param op Opcode about to be emitted.
 * @return true if a fold was applied (heap size / tail cells already
 *         mutated). false to fall through to the regular emit.
 */
static bool ff_heap_try_peephole(ff_heap_t *h, ff_opcode_t op)
{
    if (h->last_op == FF_OP_NONE)
        return false;

    /* LIT0 specializations. */
    if (h->last_op == FF_OP_LIT0)
    {
        switch (op)
        {
            case FF_OP_ADD:
            case FF_OP_SUB:
                /* `0 +` and `0 -` are no-ops — drop the LIT0, emit nothing. */
                --h->size;
                return true;
            case FF_OP_EQ:
                h->data[h->size - 1] = FF_OP_ZERO_EQ;
                return true;
            case FF_OP_NEQ:
                h->data[h->size - 1] = FF_OP_ZERO_NEQ;
                return true;
            case FF_OP_LT:
                h->data[h->size - 1] = FF_OP_ZERO_LT;
                return true;
            case FF_OP_GT:
                h->data[h->size - 1] = FF_OP_ZERO_GT;
                return true;
            default:
                break;
        }
    }

    /* LIT1 specializations. */
    if (h->last_op == FF_OP_LIT1)
    {
        switch (op)
        {
            case FF_OP_ADD:
                h->data[h->size - 1] = FF_OP_INC;
                return true;
            case FF_OP_SUB:
                h->data[h->size - 1] = FF_OP_DEC;
                return true;
            default:
                break;
        }
    }

    /* LITM1: subtracting -1 is increment, adding -1 is decrement. */
    if (h->last_op == FF_OP_LITM1)
    {
        switch (op)
        {
            case FF_OP_ADD:
                h->data[h->size - 1] = FF_OP_DEC;
                return true;
            case FF_OP_SUB:
                h->data[h->size - 1] = FF_OP_INC;
                return true;
            default:
                break;
        }
    }

    /* `i +` → FF_OP_I_ADD. Folds two dispatches plus a stack
       push/pop into one in-place add. Common in counted-loop bodies
       like `0 N 0 do  i +  loop`. */
    if (h->last_op == FF_OP_LOOP_I && op == FF_OP_ADD)
    {
        h->data[h->size - 1] = FF_OP_I_ADD;
        return true;
    }

    /* Multi-cell LIT n: tail is [LIT, n]. Replace LIT with specialized op
       and drop the value cell, or rewrite to a LITADD/LITSUB
       superinstruction (same length, fewer dispatches). */
    if (h->last_op == FF_OP_LIT)
    {
        ff_int_t n = h->data[h->size - 1];
        switch (op)
        {
            case FF_OP_ADD:
                if (n == 2)
                {
                    h->data[h->size - 2] = FF_OP_INC2;
                    --h->size;
                    return true;
                }
                h->data[h->size - 2] = FF_OP_LITADD;
                return true;
            case FF_OP_SUB:
                if (n == 2)
                {
                    h->data[h->size - 2] = FF_OP_DEC2;
                    --h->size;
                    return true;
                }
                h->data[h->size - 2] = FF_OP_LITSUB;
                return true;
            case FF_OP_MUL:
                if (n == 2)
                {
                    h->data[h->size - 2] = FF_OP_MUL2;
                    --h->size;
                    return true;
                }
                break;
            case FF_OP_DIV:
                if (n == 2)
                {
                    h->data[h->size - 2] = FF_OP_DIV2;
                    --h->size;
                    return true;
                }
                break;
            default:
                break;
        }
    }

    return false;
}

/** @copydoc ff_heap_compile_op */
void ff_heap_compile_op(ff_heap_t *h, ff_opcode_t op)
{
    if (ff_heap_try_peephole(h, op))
    {
        h->last_op = FF_OP_NONE;
        return;
    }
    ff_heap_align(h);
    ff_heap_push(h, op);
    /* Track ops that participate in non-LIT peepholes (LOOP_I → I_ADD). */
    h->last_op = (op == FF_OP_LOOP_I) ? FF_OP_LOOP_I : FF_OP_NONE;
}

/** @copydoc ff_heap_compile_lit */
void ff_heap_compile_lit(ff_heap_t *h, ff_int_t v)
{
    ff_heap_align(h);
    if (v == 0)
    {
        ff_heap_push(h, FF_OP_LIT0);
        h->last_op = FF_OP_LIT0;
    }
    else if (v == 1)
    {
        ff_heap_push(h, FF_OP_LIT1);
        h->last_op = FF_OP_LIT1;
    }
    else if (v == -1)
    {
        ff_heap_push(h, FF_OP_LITM1);
        h->last_op = FF_OP_LITM1;
    }
    else
    {
        ff_heap_push(h, FF_OP_LIT);
        ff_heap_push(h, v);
        h->last_op = FF_OP_LIT;
    }
}
