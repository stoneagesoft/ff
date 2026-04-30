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
    /* Arena-owned heaps don't free their data here — the arena
       lifetime is bound to ff_dict_destroy. */
    if (h->arena == NULL)
        free(h->data);
    memset(h, 0, sizeof(*h));
}

/* Forward decls — implementations live in ff_dict.c next to the
   arena struct definition. */
extern void *ff_arena_alloc(ff_arena_t *a, size_t bytes);
extern void  ff_arena_trim(ff_arena_t *a, void *region, size_t old_bytes,
                           size_t new_bytes);

/** @copydoc ff_heap_trim */
void ff_heap_trim(ff_heap_t *h)
{
    if (!h->arena || h->capacity == 0 || h->capacity == h->size)
        return;
    /* Ask the arena to shrink the reservation in place. The arena
       no-ops the request when this region isn't at its current bump
       point (i.e. another word has allocated since this one grew). */
    size_t old_bytes = h->capacity * sizeof(ff_int_t);
    size_t new_bytes = h->size * sizeof(ff_int_t);
    ff_arena_trim(h->arena, h->data, old_bytes, new_bytes);
    h->capacity = h->size;
}

/** @copydoc ff_heap_grow */
void ff_heap_grow(ff_heap_t *h, size_t extra)
{
    size_t nc = h->capacity
                    ? h->capacity * 2
                    : FF_INIT_HEAP_SIZE;
    while (nc < h->size + extra)
        nc *= 2;

    ff_int_t *old_data = h->data;
    if (h->arena)
    {
        /* Allocate a fresh region from the arena and copy the live
           prefix over. The old region stays in its slab as wasted
           space — acceptable internal fragmentation in exchange for
           the malloc-count win when many small words are defined. */
        ff_int_t *nd = (ff_int_t *)ff_arena_alloc(h->arena,
                                                  nc * sizeof(ff_int_t));
        if (h->size && old_data)
            memcpy(nd, old_data, h->size * sizeof(ff_int_t));
        h->data = nd;
    }
    else
    {
        h->data = (ff_int_t *)realloc(old_data, nc * sizeof(ff_int_t));
    }
    h->capacity = nc;

    /* Realloc / arena-relocation moves the buffer; the dict's sorted
       interval index is keyed on (lo, hi) pairs, so bump the mutation
       counter to force a rebuild on the next ff_addr_valid call. */
    if (h->mutation_seq_p && (h->data != old_data || extra))
        ++*h->mutation_seq_p;
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
        /* Regular opcoded built-in: single-cell opcode. Route through
           compile_op so the peephole pass sees the new op against
           h->last_op (e.g. CREATE_RUNTIME → FETCH → VAR_FETCH). */
        ff_heap_compile_op(h, w->opcode);
        return;
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
    /* Track CREATE_RUNTIME so the peephole can fold a following
       FETCH / STORE / +! into VAR_FETCH / VAR_STORE / VAR_PLUS_STORE. */
    h->last_op = (w->opcode == FF_OP_CREATE_RUNTIME)
                     ? FF_OP_CREATE_RUNTIME
                     : FF_OP_NONE;
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

    /* `i + loop` (or `i +` already folded to I_ADD, then LOOP) →
       I_ADD_LOOP. Halves the inner-loop dispatch count of typical
       summing loops. The XLOOP back-branch offset cell is emitted
       by FF_OP_LOOP after this returns true; here we just rewrite
       I_ADD in place and skip the XLOOP opcode. */
    if (h->last_op == FF_OP_I_ADD && op == FF_OP_XLOOP)
    {
        h->data[h->size - 1] = FF_OP_I_ADD_LOOP;
        return true;
    }

    /* `swap drop` → NIP. */
    if (h->last_op == FF_OP_SWAP && op == FF_OP_DROP)
    {
        h->data[h->size - 1] = FF_OP_NIP;
        return true;
    }

    /* `swap over` → TUCK. */
    if (h->last_op == FF_OP_SWAP && op == FF_OP_OVER)
    {
        h->data[h->size - 1] = FF_OP_TUCK;
        return true;
    }

    /* `over +` → OVER_PLUS. */
    if (h->last_op == FF_OP_OVER && op == FF_OP_ADD)
    {
        h->data[h->size - 1] = FF_OP_OVER_PLUS;
        return true;
    }

    /* `r@ +` → R_PLUS. */
    if (h->last_op == FF_OP_FETCH_R && op == FF_OP_ADD)
    {
        h->data[h->size - 1] = FF_OP_R_PLUS;
        return true;
    }

    /* `dup +` → DUP_ADD (TOS *= 2). One dispatch instead of three. */
    if (h->last_op == FF_OP_DUP && op == FF_OP_ADD)
    {
        h->data[h->size - 1] = FF_OP_DUP_ADD;
        return true;
    }

    /* `over over` → 2DUP. The classic Forth idiom for duplicating
       the top pair, currently emitted as two OVER ops. */
    if (h->last_op == FF_OP_OVER && op == FF_OP_OVER)
    {
        h->data[h->size - 1] = FF_OP_2DUP;
        return true;
    }

    /* `drop drop` → 2DROP. */
    if (h->last_op == FF_OP_DROP && op == FF_OP_DROP)
    {
        h->data[h->size - 1] = FF_OP_2DROP;
        return true;
    }

    /* `negate negate` → no-op (two negations cancel). Drop both. */
    if (h->last_op == FF_OP_NEGATE && op == FF_OP_NEGATE)
    {
        --h->size;
        return true;
    }

    /* `<lit> drop` → no-op for any LIT class. The literal is dead
       data and no other side effect is observable. Big win on the
       b1 empty-loop benchmark where the body is `1 drop`. */
    if (op == FF_OP_DROP)
    {
        if (h->last_op == FF_OP_LIT0
                || h->last_op == FF_OP_LIT1
                || h->last_op == FF_OP_LITM1)
        {
            --h->size;        /* drop the LIT0/1/M1 cell */
            return true;
        }
        if (h->last_op == FF_OP_LIT)
        {
            h->size -= 2;     /* drop the LIT op + value cell */
            return true;
        }
    }

    /* `<var> @` → FF_OP_VAR_FETCH, `<var> !` → FF_OP_VAR_STORE,
       `<var> +!` → FF_OP_VAR_PLUS_STORE. The CREATE_RUNTIME tail
       is [opcode, word_ptr]; we rewrite the opcode cell in place
       and skip emitting the FETCH/STORE/PLUS_STORE op. */
    if (h->last_op == FF_OP_CREATE_RUNTIME)
    {
        switch (op)
        {
            case FF_OP_FETCH:
                h->data[h->size - 2] = FF_OP_VAR_FETCH;
                return true;
            case FF_OP_STORE:
                h->data[h->size - 2] = FF_OP_VAR_STORE;
                return true;
            case FF_OP_PLUS_STORE:
                h->data[h->size - 2] = FF_OP_VAR_PLUS_STORE;
                return true;
            default:
                break;
        }
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
            case FF_OP_NEGATE:
                /* `LIT n NEGATE` → `LIT -n` — fold the unary minus
                   into the literal at compile time. Saves one
                   dispatch per occurrence. */
                h->data[h->size - 1] = -n;
                return true;
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
    /* Track ops that participate in non-LIT peepholes:
       - LOOP_I  → I_ADD
       - I_ADD   → I_ADD_LOOP
       - SWAP    → NIP / TUCK
       - OVER    → OVER_PLUS / 2DUP
       - FETCH_R → R_PLUS
       - DUP     → DUP_ADD
       - DROP    → 2DROP
       - NEGATE  → NEGATE NEGATE cancels to no-op */
    switch (op)
    {
        case FF_OP_LOOP_I:
        case FF_OP_I_ADD:
        case FF_OP_SWAP:
        case FF_OP_OVER:
        case FF_OP_FETCH_R:
        case FF_OP_DUP:
        case FF_OP_DROP:
        case FF_OP_NEGATE:
            h->last_op = op;
            break;
        default:
            h->last_op = FF_OP_NONE;
            break;
    }
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
