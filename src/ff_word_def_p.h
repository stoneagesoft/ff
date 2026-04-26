/**
 * @file ff_word_def_p.h
 * @brief Static-table entry used to register built-in and external
 *        Forth words.
 *
 * Two registration macro families:
 *
 *   - @c _FF_W / @c _FF_WI  — internal: built-in opcoded word
 *     (function pointer is NULL; the case body lives in
 *     `words/ff_words_*_p.h`).
 *   - @c FF_W / @c FF_WI    — public: external native word added by an
 *     embedder. Carries a function pointer; at runtime it goes
 *     through the FF_OP_CALL escape hatch.
 *
 * The leading-underscore names denote library-internal use; embedder
 * code should reach for the underscore-free variants.
 */

#pragma once

#include <ff_word_p.h>


/**
 * @struct ff_word_def
 * @brief One row of a NULL-terminated registration table.
 *
 * Tables of these structs live next to each category's word
 * implementation file (e.g. @c FF_MATH_WORDS in ff_words_math.c) and
 * are walked by @ref ff_dict_define at startup.
 */
typedef struct ff_word_def
{
    const char *name;       /**< Forth name; NULL marks end-of-table (see @ref FF_WEND). */
    bool is_immediate;      /**< If true, the word is compile-time (FF_WORD_IMMEDIATE). */
    ff_word_fn code;        /**< External native fn pointer; NULL for opcoded built-ins. */
    ff_opcode_t opcode;     /**< Engine opcode (FF_OP_NONE for external natives). */
    const char *manual;     /**< Markdown manual entry; first line is the prototype. */
} ff_word_def_t;


/** @brief Built-in opcoded word with a manual entry. */
#define _FF_W(name, op, man)      { name, false, NULL, op, man }
/** @brief Built-in opcoded immediate word. */
#define _FF_WI(name, op, man)     { name, true,  NULL, op, man }

/** @brief External native word (FF_OP_CALL escape hatch). */
#define FF_W(name, fn, man)       { name, false, fn,   FF_OP_NONE, man }
/** @brief External native immediate word. */
#define FF_WI(name, fn, man)      { name, true,  fn,   FF_OP_NONE, man }

/** @brief Sentinel terminating a registration table. */
#define FF_WEND                   { NULL,  false, NULL, FF_OP_NONE, NULL }
