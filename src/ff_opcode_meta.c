/**
 * @file ff_opcode_meta.c
 * @brief Operand-layout table indexed by opcode.
 *
 * Adding an opcode means a line in FF_OPCODES (ff_opcode_p.h), which
 * gives it its enum value and its layout here, and its case body in the
 * dispatch include; a built-in word also gets a row in its registration
 * table.
 */

#include "ff_opcode_meta_p.h"

#include <stddef.h>


/* Indexed by opcode value, generated from FF_OPCODES. */
static const ff_op_layout_t g_layout[FF_OP_COUNT] =
{
#define FF_OP_LAYOUT_ROW_(name, layout) [FF_OP_##name] = FF_OP_LAYOUT_##layout,
    FF_OPCODES(FF_OP_LAYOUT_ROW_)
#undef FF_OP_LAYOUT_ROW_
};


/** @copydoc ff_opcode_layout */
ff_op_layout_t ff_opcode_layout(ff_opcode_t op)
{
    if (op < 0 || op >= FF_OP_COUNT)
        return FF_OP_LAYOUT_NONE;
    return g_layout[op];
}

size_t ff_opcode_encoded_cells(ff_opcode_t op,
                               const ff_int_t *cells,
                               size_t pos, size_t size)
{
    switch (ff_opcode_layout(op))
    {
        case FF_OP_LAYOUT_NONE:
            return 1;
        case FF_OP_LAYOUT_INT:
        case FF_OP_LAYOUT_REAL:
        case FF_OP_LAYOUT_WORD:
        case FF_OP_LAYOUT_FN:
            return 2;
        case FF_OP_LAYOUT_STR:
            return 1 + (pos + 1 < size ? (size_t)cells[pos + 1] : 0);
    }
    return 1;
}
