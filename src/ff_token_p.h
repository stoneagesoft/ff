/**
 * @file ff_token_p.h
 * @brief Token kinds returned by ff_tokenizer_next().
 *
 * The tokenizer classifies each whitespace-delimited lexeme into one
 * of these categories before handing it to the evaluator, which
 * dispatches on the kind: integers/reals push (or compile a literal),
 * strings push a temporary buffer (or compile inline bytes), words go
 * through dictionary lookup.
 */

#pragma once

/**
 * @enum ff_token
 * @brief Lexical category of the most recently scanned token.
 */
typedef enum ff_token
{
    FF_TOKEN_NULL    = 0,   /**< End of input; no more tokens on this source. */
    FF_TOKEN_WORD,          /**< Identifier — looked up in the dictionary. */
    FF_TOKEN_INTEGER,       /**< Integer literal; value lives in @ref ff_tokenizer::integer_val. */
    FF_TOKEN_REAL,          /**< Floating-point literal; value lives in @ref ff_tokenizer::real_val. */
    FF_TOKEN_STRING         /**< String literal between quotes; bytes live in @ref ff_tokenizer::token. */
} ff_token_t;
