/**
 * @file ff_tok_state_p.h
 * @brief Tokenizer state flags.
 *
 * Currently a single bit; kept as a separate header so future flags
 * (e.g. for nested string syntaxes) can be added without changing
 * any other file's includes.
 */

#pragma once

/**
 * @enum ff_tok_state
 * @brief OR-able state bits for @ref ff_tokenizer::state.
 */
typedef enum ff_tok_state
{
    FF_TOK_STATE_COMMENT = 1 << 0   /**< Currently inside an open `(` comment that started on a previous line. */
} ff_tok_state_t;
