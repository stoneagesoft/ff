/**
 * @file ff_word_p.h
 * @brief Dictionary word descriptor and lifecycle helpers.
 *
 * After the opcode-dispatch migration a built-in word is little more
 * than a name + an opcode; the case body lives in the per-category
 * `words/ff_words_*_p.h` dispatch include. External natives use the
 * FF_OP_CALL escape hatch and stash their fn pointer in heap.data[0].
 * Colon-defs compile bytecode into the heap and may set `does` to a
 * DOES>-clause entry point.
 */

#pragma once

#include <ff_heap_p.h>
#include <ff_opcode_p.h>
#include <ff_scope_p.h>
#include <ff_types_p.h>
#include <ff_word_flags_p.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>


/**
 * @def ff_strdup
 * @brief Portable @c strdup spelling.
 *
 * MSVC ships this as @c _strdup; POSIX, GNU, BSD, and macOS expose
 * the unprefixed name.
 */
#if defined(_MSC_VER)
#define ff_strdup _strdup
#else
#define ff_strdup strdup
#endif


typedef struct ff ff_t;
typedef struct ff_word ff_word_t;
/**
 * @brief External native-word entry point.
 *
 * Receives the engine instance; reads/writes the data and return
 * stacks through @ref ff::stack and @ref ff::r_stack and reports
 * errors via ff_tracef().
 */
typedef void (*ff_word_fn)(ff_t *ff);

/**
 * Allocate and initialize a heap-resident ff_word_t.
 *
 * Used for user definitions (`:`, `variable`, `constant`, …) and
 * external native words registered after dict init. Built-ins go
 * through @ref ff_word_init_static instead so they can sit in the
 * dict's pre-allocated pool.
 *
 * @param name   Word name; @c strdup'd into the new word.
 * @param code   Optional native fn pointer (NULL for opcoded words).
 * @param opcode Opcode that drives execution (FF_OP_NONE for natives).
 * @param manual Manual entry; first line is the prototype, rest is
 *               the synopsis. May be NULL.
 * @return Newly allocated word, owned by the caller (typically the
 *         dictionary).
 */
ff_word_t *ff_word_new(const char *name, ff_word_fn code,
                       ff_opcode_t opcode, const char *manual);

/**
 * Same as @ref ff_word_new but also sets @ref FF_WORD_IMMEDIATE.
 *
 * @param name   Word name.
 * @param code   Optional native fn pointer.
 * @param opcode Opcode.
 * @param manual Manual entry; may be NULL.
 * @return Newly allocated immediate word.
 */
ff_word_t *ff_im_word_new(const char *name, ff_word_fn code,
                          ff_opcode_t opcode, const char *manual);

/**
 * Release a word and everything it owns. Words tagged @ref
 * FF_WORD_STATIC keep their struct and name (both belong to a static
 * pool) but their heap is still released because external natives
 * stash a fn pointer there.
 *
 * @param w Word to free, or NULL.
 */
void ff_word_free(ff_word_t *w);

/**
 * Initialize an existing ff_word_t in place — used by the built-in
 * pool path. @p name is taken by reference (not strdup'd) and must
 * outlive the dict; the struct is tagged @ref FF_WORD_STATIC so
 * ff_word_free skips releasing it.
 *
 * @param w      Pre-allocated storage (typically a slot in @ref
 *               ff_dict::static_pool).
 * @param name   Word name; lifetime ≥ dict lifetime (string literal).
 * @param code   Optional native fn pointer.
 * @param opcode Opcode.
 * @param manual Manual entry; may be NULL.
 */
void ff_word_init_static(ff_word_t *w, const char *name, ff_word_fn code,
                         ff_opcode_t opcode, const char *manual);

/**
 * Record the source text of a scope signature against the bytecode
 * offset of its FF_OP_SCOPE_ENTER.
 *
 * Consulted only by `see`; nothing on the execution path reads it.
 *
 * @param w    Word being compiled.
 * @param off  Index of the FF_OP_SCOPE_ENTER cell in @c w->heap.
 * @param text Signature source, copied.
 * @return false on allocation failure (the signature is simply not
 *         recorded; compilation can continue).
 */
bool ff_word_add_sig(ff_word_t *w, size_t off, const char *text);

/**
 * Look up the signature recorded at bytecode offset @p off.
 *
 * @param w   Word.
 * @param off Index of an FF_OP_SCOPE_ENTER cell.
 * @return Signature text, or NULL if none was recorded.
 */
const char *ff_word_sig_at(const ff_word_t *w, size_t off);

/**
 * Test whether @p w has no compiled Forth body. True for built-ins
 * (empty heap) and external natives (FF_WORD_NATIVE flag); false for
 * colon-defs and DOES>/CREATE-runtime words that store data or
 * bytecode in their heap.
 *
 * @param w Word to inspect.
 * @return true iff @p w should be displayed as native by `see`.
 */
bool ff_word_is_native(const ff_word_t *w);

/**
 * Retrieve the native function pointer of an external native word.
 * Stashed at heap.data[0]; valid only when
 * `(w->flags & FF_WORD_NATIVE)`.
 *
 * @param w Word with FF_WORD_NATIVE set.
 * @return Function pointer dispatched by FF_OP_CALL.
 */
ff_word_fn ff_word_native_fn(const ff_word_t *w);


/**
 * @struct ff_word
 * @brief One dictionary entry.
 *
 * Memory ownership depends on @ref ff_word::flags: words flagged
 * FF_WORD_STATIC live in the dict's static pool with @ref name
 * pointing at a string literal; everything else is heap-allocated.
 */
struct ff_word
{
    char *name;                 /**< Word name (strdup'd, or aliasing a literal for static words). */
    ff_opcode_t opcode;         /**< Engine opcode driving execution; FF_OP_NONE = no opcode. */
    ff_word_flags_t flags;      /**< OR of FF_WORD_* flags. */
    ff_int_t *does;             /**< DOES> clause IP (NULL if none). */
    ff_heap_t heap;             /**< Compiled bytecode (colon-defs) or fn pointer (natives). */
    ff_sig_t *sigs;             /**< Scope signatures by bytecode offset, for `see`; NULL if none. */
    size_t sigs_len;            /**< Count of @ref sigs. */
    const char *manual;         /**< Markdown manual entry; may be NULL. */
    const char *man_desc;       /**< Points into @ref manual past the first newline. */
    struct ff_word *next_bucket;/**< Singly-linked dict hash chain (newest-first). */
};
