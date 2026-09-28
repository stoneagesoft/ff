/**
 * @file ff_opcode_meta_p.h
 * @brief Per-opcode operand layout.
 *
 * The dispatch loop in ff_exec doesn't consult this table — its switch
 * is hand-written for performance. It is for code that walks or emits
 * bytecode: the `see` decompiler, `dump-word`, the tail-call check in
 * `;`, the execution stubs, and compiling a call to a word. The table
 * is generated from FF_OPCODES (ff_opcode_p.h), so adding an opcode there
 * gives it a layout here. Names come from the built-in word tables.
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
 * @brief Operand layout of @p op: what follows it in compiled code.
 *        FF_OP_LAYOUT_NONE for FF_OP_NONE or a value that isn't an
 *        opcode.
 */
ff_op_layout_t ff_opcode_layout(ff_opcode_t op);

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
