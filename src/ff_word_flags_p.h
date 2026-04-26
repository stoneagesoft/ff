/**
 * @file ff_word_flags_p.h
 * @brief Per-word boolean flags packed into @ref ff_word::flags.
 *
 * Flags are OR'd together. Some are user-visible (`immediate`,
 * `hidden`); others (`native`, `static`) are engine-internal and
 * track the storage strategy of a word.
 */

#pragma once

/**
 * @enum ff_word_flag
 * @brief Bit flags for @ref ff_word::flags.
 */
typedef enum ff_word_flag
{
    FF_WORD_IMMEDIATE = 1 << 0,  /**< Compile-time word: runs even while compiling. */
    FF_WORD_USED      = 1 << 1,  /**< Set on first lookup; powers the `wordsused` introspection. */
    FF_WORD_HIDDEN    = 1 << 2,  /**< Skip in `words` listings. */
    FF_WORD_NATIVE    = 1 << 3,  /**< External native: function pointer at heap.data[0]. */
    FF_WORD_STATIC    = 1 << 4   /**< Built-in stored in the dict's static pool — name and struct
                                      are not individually freed. */
} ff_word_flag_t;

/**
 * @brief Storage type for an OR of ff_word_flag values.
 */
typedef unsigned ff_word_flags_t;
