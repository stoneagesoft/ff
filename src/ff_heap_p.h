/**
 * @file ff_heap_p.h
 * @brief Per-word growable cell array used for compiled bytecode and
 *        runtime data.
 *
 * Every ff_word_t owns a heap into which the colon-def compiler emits
 * opcodes and operands, and into which @c create / @c variable /
 * @c constant words store their per-instance data. Cells are
 * @ref ff_int_t (intptr-sized). The heap doubles in capacity on
 * demand and can also store sub-cell bytes via @ref ff_heap_compile_char.
 *
 * The compile path runs a tiny peephole optimizer (see
 * @ref ff_heap::last_op) that folds (literal, op) pairs into
 * specialized opcodes — e.g. `LIT 1 +` → `INC`.
 */

#pragma once

#include <ff_config_p.h>
#include <ff_opcode_p.h>
#include <ff_types_p.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


typedef struct ff_heap ff_heap_t;
typedef struct ff_word ff_word_t;

/**
 * Initialize a heap to empty (no allocation).
 * @param h Heap to initialize.
 */
void ff_heap_init(ff_heap_t *h);

/**
 * Release the heap's data buffer and zero the struct.
 * @param h Heap to destroy.
 */
void ff_heap_destroy(ff_heap_t *h);

/**
 * Round the byte cursor up to the next cell boundary so the next
 * push lands at a clean ff_int_t address.
 * @param h Heap.
 */
void ff_heap_align(ff_heap_t *h);

/**
 * Append @p cells zeroed cells to the heap (Forth `allot`).
 * @param h     Heap.
 * @param cells Number of @ref ff_int_t-sized cells to reserve. Must
 *              be > 0.
 */
void ff_heap_alloc(ff_heap_t *h, size_t cells);

/**
 * Emit @p w as a callable inside a colon-def — chooses between
 * NEST + ptr / opcoded form / FF_OP_CALL + fn pointer based on @p w's
 * shape.
 * @param h Heap.
 * @param w Word being referenced.
 */
void ff_heap_compile_word(ff_heap_t *h, const ff_word_t *w);

/**
 * Emit a single ff_int_t cell. Clears the peephole's last-op tracker.
 * @param h Heap.
 * @param v Value to write.
 */
void ff_heap_compile_int(ff_heap_t *h, ff_int_t v);

/**
 * Emit a real value as a single cell via memcpy bit-pattern. Clears
 * the peephole's last-op tracker.
 * @param h Heap.
 * @param r Real value.
 */
void ff_heap_compile_real(ff_heap_t *h, ff_real_t r);

/**
 * Append a single byte, packing it into the current cell. Clears the
 * peephole's last-op tracker.
 * @param h Heap.
 * @param c Byte to write.
 */
void ff_heap_compile_char(ff_heap_t *h, char c);

/**
 * Emit an inline-string literal: `[skip cell, packed bytes…]`. The
 * skip cell tells the runtime how many cells to advance past the
 * payload.
 * @param h   Heap.
 * @param s   Bytes to inline.
 * @param len Number of bytes in @p s.
 */
void ff_heap_compile_str(ff_heap_t *h, const char *s, size_t len);

/**
 * Emit a single-cell opcode, applying a peephole pass against the
 * previous literal (if any). Folds e.g. `LIT 1 +` into `INC` —
 * see ff_heap.c::ff_heap_try_peephole for the full table.
 * @param h  Heap.
 * @param op Opcode to emit.
 */
void ff_heap_compile_op(ff_heap_t *h, ff_opcode_t op);

/**
 * Emit an integer-literal sequence with specialization: 0 / 1 / -1
 * become single-cell @c FF_OP_LIT0 / @c FF_OP_LIT1 / @c FF_OP_LITM1;
 * everything else is @c FF_OP_LIT plus an operand cell. Sets the
 * peephole tracker so a follow-up @ref ff_heap_compile_op can fuse.
 * @param h Heap.
 * @param v Literal value.
 */
void ff_heap_compile_lit(ff_heap_t *h, ff_int_t v);


/**
 * @struct ff_heap
 * @brief Growable cell array backing one word.
 */
struct ff_heap
{
    ff_int_t *data;     /**< Cell storage; reallocated on growth. */
    size_t size;        /**< Number of valid cells. */
    size_t capacity;    /**< Allocated cells in @ref data. */
    uint8_t byte_off;   /**< Sub-cell byte cursor used by @ref ff_heap_compile_char. */

    /**
     * Peephole bookkeeping. Set by @ref ff_heap_compile_lit to the
     * literal-class opcode it just emitted (FF_OP_LIT / LIT0 / LIT1 /
     * LITM1); cleared by every other compile_* path.
     * @ref ff_heap_compile_op uses it to fold (literal, op) pairs into
     * specialized single ops or superinstructions (e.g. LIT 1 + → INC,
     * LIT 5 + → LITADD 5).
     */
    ff_opcode_t last_op;
};

/**
 * Clear the peephole tracker so the next @ref ff_heap_compile_op
 * can't fuse with whatever was emitted before. Called by every
 * control-flow immediate (IF/THEN/ELSE/BEGIN/…) so a fold doesn't
 * cross a branch target / back-branch boundary.
 * @param h Heap.
 */
static inline void ff_heap_inhibit_peephole(ff_heap_t *h)
{
    h->last_op = FF_OP_NONE;
}

/**
 * Ensure @p h has room for @p extra additional cells, doubling
 * capacity as needed.
 * @param h     Heap.
 * @param extra Cells beyond @ref ff_heap::size.
 */
static inline void ff_heap_ensure(ff_heap_t *h, size_t extra)
{
    if (h->size + extra > h->capacity)
    {
        size_t nc = h->capacity
                        ? h->capacity * 2
                        : FF_INIT_HEAP_SIZE;
        while (nc < h->size + extra)
            nc *= 2;
        h->data = (ff_int_t *)realloc(h->data, nc * sizeof(ff_int_t));
        h->capacity = nc;
    }
}

/**
 * Push a single ff_int_t cell, growing the buffer as needed. Does
 * NOT touch the peephole's last-op tracker — callers that emit
 * meaningful opcodes should go through @ref ff_heap_compile_op.
 * @param h Heap.
 * @param v Cell value.
 */
static inline void ff_heap_push(ff_heap_t *h, ff_int_t v)
{
    ff_heap_ensure(h, 1);
    h->data[h->size++] = v;
}
