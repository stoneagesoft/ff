/**
 * @file ff_word_def_p.h
 * @brief Static-table entry used to register the built-in Forth words.
 *
 * @c _FF_W / @c _FF_WI make a row for a built-in opcoded word (function
 * pointer NULL; the case body lives in `words/ff_words_*_p.h`). An
 * embedder's native words are registered with FF_NATIVE() and
 * ff_register() instead (ff.h).
 */

#pragma once

#include <ff_tr.h>
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
    const char *manual;     /**< Markdown manual entry; first line is the prototype. Every
                                 language in one string, read with ff_tr_copy() (see _FF_MAN). */
} ff_word_def_t;


/**
 * @brief A built-in word's manual, as the row macros store it: @p stack,
 *        the same in every language — the stack effect, after a parse
 *        marker for a word that reads a name (`w`) or a string (`s`) —
 *        then the title and text in each language, 1 to FF_TR_MAX of
 *        them, each marked: `FF_TR_EN("Duplicate\n…")`, `FF_TR_RU(…)`.
 *        One string, "( n -- n n )  " then the translations, which `man`
 *        reads with ff_tr_copy() in the engine's language.
 *
 * In the check build (CMake FF_TR_ONLY) the stack effect is left out, so
 * that a row missing the language kept leaves the field empty and the
 * build fails (-Wmissing-field-initializers).
 */
#if FF_WITH_TR && defined(FF_TR_ONE_)
#  define _FF_MAN(stack, ...)  FF_TR_JOIN(__VA_ARGS__)
#else
#  define _FF_MAN(stack, ...)  stack "  " FF_TR_JOIN(__VA_ARGS__)
#endif

/** @brief Built-in opcoded word: name, opcode, stack effect, then the
 *         manual's translations (see _FF_MAN). */
#define _FF_W(name, op, stack, ...)   { name, false, NULL, op, _FF_MAN(stack, __VA_ARGS__) }
/** @brief Built-in opcoded immediate word; the manual as for _FF_W. */
#define _FF_WI(name, op, stack, ...)  { name, true,  NULL, op, _FF_MAN(stack, __VA_ARGS__) }

/** @brief Sentinel terminating a registration table. */
#define FF_WEND                   { NULL,  false, NULL, FF_OP_NONE, NULL }
