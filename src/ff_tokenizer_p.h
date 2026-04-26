/**
 * @file ff_tokenizer_p.h
 * @brief Stateful whitespace-delimited Forth tokenizer.
 *
 * The tokenizer scans a source buffer character by character starting
 * at an explicit `*pos`, classifies the next lexeme, and returns one
 * of the @ref ff_token kinds. It carries enough state to keep
 * `( … )` comments and string-literal anticipation alive across
 * lines, which is why ff_load() rewinds and re-uses the same
 * tokenizer struct between calls to ff_eval().
 */

#pragma once

#include <ff_config_p.h>
#include <ff_tok_state_p.h>
#include <ff_token_p.h>
#include <ff_types_p.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct ff_tokenizer ff_tokenizer_t;

/**
 * Initialize a tokenizer to a clean (interpret-mode) state.
 * @param t Tokenizer to initialize.
 */
void ff_tokenizer_init(ff_tokenizer_t *t);

/**
 * Tear down a tokenizer. No-op today; provided for symmetry.
 * @param t Tokenizer to destroy.
 */
void ff_tokenizer_destroy(ff_tokenizer_t *t);

/**
 * Read the next token from @p src starting at `*pos`, advancing the
 * cursor past it. The returned classification dictates which of the
 * tokenizer's value fields holds useful data:
 *
 *   - FF_TOKEN_WORD     → @ref ff_tokenizer::token / token_len
 *   - FF_TOKEN_INTEGER  → @ref ff_tokenizer::integer_val
 *   - FF_TOKEN_REAL     → @ref ff_tokenizer::real_val
 *   - FF_TOKEN_STRING   → @ref ff_tokenizer::token / token_len
 *   - FF_TOKEN_NULL     → no further tokens; @p pos is unchanged
 *
 * @param t   Tokenizer (carries comment state across calls).
 * @param src Source text; must remain valid for the call.
 * @param pos In/out cursor into @p src; updated to point past the
 *            consumed token on success.
 * @return The classification of the just-scanned token.
 */
ff_token_t ff_tokenizer_next(ff_tokenizer_t *t, const char *src, int *pos);


/**
 * @struct ff_tokenizer
 * @brief Persistent tokenizer state.
 *
 * One instance lives inside @ref ff::tokenizer. The token buffer is
 * inline so a tokenizer can be allocated on the stack for testing.
 */
struct ff_tokenizer
{
    ff_tok_state_t state;            /**< OR of FF_TOK_STATE_* flags (e.g. open `(` comment). */
    int line;                        /**< Source line number (advanced by ff_load()). */
    int pos;                         /**< Byte position of the most recent token's start. */
    char token[FF_TOKEN_SIZE];       /**< NUL-terminated token text or string-literal payload. */
    size_t token_len;                /**< Length of @ref token, excluding the NUL. */
    ff_int_t integer_val;            /**< Decoded integer when token kind is FF_TOKEN_INTEGER. */
    ff_real_t real_val;              /**< Decoded real when token kind is FF_TOKEN_REAL. */
};

/**
 * Read one byte from @p src at `*pos` and advance past it.
 * @param src Source text.
 * @param pos In/out cursor.
 * @return The byte read, cast through int (no sign extension).
 */
static int ff_tok_get(const char *src, int *pos);

/**
 * Test whether the cursor has reached end-of-input.
 * @param src Source text.
 * @param pos Cursor position.
 * @return true iff `src[pos]` is the terminating NUL.
 */
static bool ff_tok_eof(const char *src, int pos);
