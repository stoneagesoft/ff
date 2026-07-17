/**
 * @file ff_scope_p.h
 * @brief Records backing the `{ ( a b -- c ) … }` stack-scope construct.
 *
 * A scope walls off the data stack: code between `{` and `}` sees an
 * empty stack, reaches its inputs only through their declared names,
 * and is checked at `}` for having produced exactly what it promised.
 *
 * Two record types, with different lifetimes:
 *
 * - @ref ff_scope is *run-time* state. One record per open barrier,
 *   pushed by FF_OP_SCOPE_ENTER and popped by FF_OP_SCOPE_EXIT. Its
 *   depth is bounded by **call** depth, not lexical nesting: a
 *   recursive scoped word holds one record per active invocation.
 *
 * - @ref ff_csig is *compile-time* state. One record per open `{` in
 *   the source being compiled, carrying the parsed signature so a token
 *   can be resolved to an FF_OP_ARG index. Lexically nested, hence tiny.
 */

#pragma once

#include <ff_config_p.h>

#include <stdbool.h>
#include <stdint.h>


/** @brief Maximum length of a signature's source text, in bytes. */
#define FF_SIG_TEXT_MAX 256


/**
 * @struct ff_scope
 * @brief One open run-time barrier.
 */
typedef struct ff_scope
{
    uint32_t floor;   /**< Enclosing @ref ff_stack::floor, restored at `}`. */
    uint32_t r_top;   /**< Return-stack depth at `{`; asserted unchanged at `}`. */
} ff_scope_t;


/**
 * @struct ff_sig
 * @brief A scope's signature text, kept for `see`.
 *
 * Held in a side table on the owning @ref ff_word rather than inline in
 * the bytecode, so the run time never pays for it: FF_OP_SCOPE_ENTER
 * carries only its packed operand, and this is consulted solely by the
 * decompiler.
 *
 * Keyed by bytecode offset because signatures are per-*scope*, not
 * per-word — one word may open several, nested. Stored as source text
 * rather than a parsed name array so `...` and anything a future
 * notation allows round-trip without a schema change; `see` re-splits it
 * when it needs a name.
 */
typedef struct ff_sig
{
    size_t ip_offset;  /**< Index of the FF_OP_SCOPE_ENTER cell this describes. */
    char  *text;       /**< Owned. Signature as written, e.g. "( x1 y1 x2 y2 -- d )". */
} ff_sig_t;


/**
 * @enum ff_csig_phase
 * @brief Where the signature parser is within `( a b -- c )`.
 */
typedef enum ff_csig_phase
{
    FF_CSIG_EXPECT_OPEN = 0,  /**< Waiting for the literal `(`. */
    FF_CSIG_INPUTS,           /**< Collecting named inputs, up to `--`. */
    FF_CSIG_OUTPUTS           /**< Counting outputs, up to `)`. */
} ff_csig_phase_t;


/**
 * @struct ff_csig
 * @brief Compile-time signature of one open `{`.
 *
 * @ref names is ordered as written, so `( a b c -- )` stores a, b, c at
 * indices 0, 1, 2. The rightmost name is the top of the caller's stack,
 * so name index @c i compiles to `FF_OP_ARG` with k = nargs - i.
 */
typedef struct ff_csig
{
    char *names[FF_SCOPE_ARGS_MAX]; /**< Named inputs, as written; owned (strdup'd). */
    char  text[FF_SIG_TEXT_MAX];    /**< Signature source, rebuilt token by token, for @ref ff_sig. */
    int   text_len;                 /**< Bytes used in @ref text. */
    int   nargs;                    /**< Count of @ref names. */
    int   nouts;                    /**< Declared outputs; meaningless when @ref var_out. */
    bool  var_in;                   /**< `( ... -- ` : inherit the enclosing barrier, no names. */
    bool  var_out;                  /**< ` -- ... )` : skip the arity check at `}`. */
    int   phase;                    /**< An @ref ff_csig_phase value. */
} ff_csig_t;
