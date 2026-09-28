/**
 * @file ff_cf_p.h
 * @brief Compile-time control-flow stack: one record per open `if`,
 *        `begin`, `do`, … in the definition being compiled.
 *
 * Standard Forth may keep control-flow items on the data stack; *ff*
 * keeps them apart and typed. A number on the data stack can then never
 * be taken for a branch to patch (`[ 100000 ] then` wrote to heap cell
 * 100000), and every record knows what opened it, so a closer that
 * doesn't match (`begin … then`, `do … until`), a structure that crosses
 * a `{ }` scope, or one still open at `;` is reported where it is written
 * instead of compiling a jump into the weeds.
 */

#pragma once

#include <stddef.h>


/**
 * @enum ff_cf_kind
 * @brief What a control-flow record stands for (the ANS orig / dest /
 *        do-sys).
 */
typedef enum ff_cf_kind
{
    FF_CF_ORIG,     /**< Forward branch awaiting its target: `if`, `else`, `while`. */
    FF_CF_DEST,     /**< Target of a backward branch: `begin`. */
    FF_CF_DO        /**< Counted loop: `do`, `?do`. */
} ff_cf_kind_t;

/**
 * @struct ff_cf
 * @brief One open control structure.
 */
typedef struct ff_cf
{
    ff_cf_kind_t kind;      /**< Record type. */
    const char  *opener;    /**< Word that opened it (`if`, `?do`, …), for diagnostics. */
    size_t       pos;       /**< ORIG: the branch offset cell to patch. DEST: the branch
                                 target. DO: the first cell of the loop body. */
    int          scope;     /**< ff::n_csig when opened: the structure must close in the
                                 same `{ }` scope, since a branch across a barrier would
                                 skip its SCOPE_ENTER or SCOPE_EXIT. */
} ff_cf_t;
