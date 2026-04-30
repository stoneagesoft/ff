/**
 * @file ff_opcode_meta_p.h
 * @brief Per-opcode metadata: operand layout and Forth-syntax name.
 *
 * The dispatch loop in ff_exec doesn't consult this table — its switch
 * is hand-written for performance. This data is for tooling: the `see`
 * decompiler, the bytecode walker that backs `dump-word`, and any
 * future packed-encoding rewrite or disassembly export.
 *
 * One source of truth means adding a new opcode (especially a peephole
 * superinstruction) updates this table once instead of touching
 * see_opcode_len, see_decompile_body, and any other tooling switch.
 */

#pragma once

#include <ff_opcode_p.h>
#include <ff_types_p.h>

#include <stdbool.h>
#include <stddef.h>


/**
 * @enum ff_op_layout
 * @brief How an opcode is encoded in the heap.
 */
typedef enum ff_op_layout
{
    FF_OP_LAYOUT_NONE = 0,    /**< Single cell: just the opcode. */
    FF_OP_LAYOUT_INT,         /**< Opcode + one cell carrying an integer / pointer / offset. */
    FF_OP_LAYOUT_REAL,        /**< Opcode + one cell carrying a real bit-pattern. */
    FF_OP_LAYOUT_WORD,        /**< Opcode + one cell carrying a ff_word_t pointer. */
    FF_OP_LAYOUT_FN,          /**< Opcode + one cell carrying an external native fn pointer. */
    FF_OP_LAYOUT_STR          /**< Opcode + skip-count cell + packed bytes. */
} ff_op_layout_t;

/**
 * @struct ff_opcode_meta
 * @brief Layout and identifier information for one opcode.
 */
typedef struct ff_opcode_meta
{
    /** Forth-source spelling, or NULL when the opcode has no surface
     *  syntax (control-flow internals like XDO/XLOOP, runtime entries). */
    const char *name;
    ff_op_layout_t layout;
} ff_opcode_meta_t;

/**
 * @brief Look up metadata for @p op.
 *
 * Returns a pointer to a static table entry. Callers can read
 * .layout to size the encoded form (see @ref ff_opcode_encoded_cells)
 * or .name for a default-case spelling.
 */
const ff_opcode_meta_t *ff_opcode_meta(ff_opcode_t op);

/**
 * @brief Encoded cell count for @p op at heap @p cells, position @p pos.
 *
 * For STR-layout opcodes the count depends on the inline skip-count
 * cell; for everything else it's a static property of the opcode.
 *
 * @param op    Opcode at @c cells[pos].
 * @param cells Compiled heap.
 * @param pos   Position of @p op in @p cells.
 * @param size  Total cell count in @p cells (bound for STR reads).
 * @return Number of cells the opcode + its operands occupy.
 */
size_t ff_opcode_encoded_cells(ff_opcode_t op,
                               const ff_int_t *cells,
                               size_t pos, size_t size);
