/**
 * @file ff_dict_p.h
 * @brief Forth dictionary: ordered list of words plus a hash index.
 *
 * The dictionary keeps two views of the same set of words:
 *
 *   - An ordered array (@ref ff_dict::words) preserving insertion
 *     order; this is what `words`, `see`, FORGET, and the latest-word
 *     lookups iterate.
 *   - A power-of-two hash table (@ref ff_dict::buckets) indexed by an
 *     ASCII-case-folded FNV-1a of the name; this is the O(1) lookup
 *     path. Buckets are singly-linked through @ref ff_word::next_bucket.
 *
 * Built-in words live in a single pre-allocated pool (@ref
 * ff_dict::static_pool) so dict init is one big calloc instead of
 * ~150 individual mallocs.
 */

#pragma once

#include <ff_word_def_p.h>

#include <stdbool.h>
#include <stddef.h>


typedef struct ff_dict ff_dict_t;

/**
 * Initialize a dictionary, allocate the bucket array and static pool,
 * and register every built-in word.
 * @param d Dictionary to initialize.
 */
void ff_dict_init(ff_dict_t *d);

/**
 * Release every dynamically allocated word, the buckets, and the
 * static pool.
 * @param d Dictionary to destroy.
 */
void ff_dict_destroy(ff_dict_t *d);

/**
 * @param d Dictionary.
 * @return Most recently appended word, or NULL if the dict is empty.
 *         Used by the compile-time machinery to address the
 *         currently-being-built definition.
 */
ff_word_t *ff_dict_top(ff_dict_t *d);

/**
 * Look up a word by name, case-insensitively. Sets @ref FF_WORD_USED
 * on a hit so the `wordsused` introspection can report touched
 * built-ins.
 *
 * @param d    Dictionary.
 * @param name Name to search for (NUL-terminated).
 * @return Matching word, or NULL when not found.
 */
ff_word_t *ff_dict_lookup(ff_dict_t *d, const char *name);

/**
 * Append @p w as the newest word and link it into its hash bucket.
 *
 * @param d Dictionary.
 * @param w Word to append; must outlive @p d unless freed via
 *          ff_dict_forget() / ff_dict_destroy().
 * @return @p w, for convenient chaining.
 */
ff_word_t *ff_dict_append(ff_dict_t *d, ff_word_t *w);

/**
 * Atomically rename a word and re-bucket it under the new name. Used
 * by the compiler when the placeholder name (`" "`) for a fresh
 * colon-def is replaced with the user-supplied identifier.
 *
 * @param d        Dictionary.
 * @param w        Word to rename. Must not be FF_WORD_STATIC.
 * @param new_name Replacement name; @c strdup'd into @p w.
 */
void ff_dict_rename(ff_dict_t *d, ff_word_t *w, const char *new_name);

/**
 * Remove @p name and every later-defined word from the dictionary
 * (Forth's FORGET). Rebuilds the bucket index after truncation.
 *
 * @param d    Dictionary.
 * @param name Name of the word marking the cut point.
 * @return true on success, false if @p name was not found.
 */
bool ff_dict_forget(ff_dict_t *d, const char *name);

/**
 * Append every entry of a NULL-terminated @ref ff_word_def_t table
 * via @ref ff_word_new (i.e. heap-allocated). The static-pool path
 * used by @ref ff_dict_init is internal.
 *
 * @param d    Dictionary.
 * @param defs Sentinel-terminated table.
 */
void ff_dict_define(ff_dict_t *d, const ff_word_def_t *defs);


/**
 * @struct ff_dict
 * @brief Dictionary internals.
 */
struct ff_dict
{
    /**
     * Ordered array (newest at end) — used by ff_dict_top,
     * ff_dict_forget, and introspection (`words`, `see`, …).
     */
    ff_word_t **words;
    size_t count;          /**< Number of valid entries in @ref words. */
    size_t capacity;       /**< Allocated length of @ref words. */

    /**
     * Hash buckets for O(1) name lookup. Each bucket is a singly-linked
     * list, newest first, threaded through @ref ff_word::next_bucket.
     * The array size is a power of two so masking replaces modulo;
     * rebuilt wholesale by @ref ff_dict_forget.
     */
    ff_word_t **buckets;
    size_t bucket_count;   /**< Power-of-two bucket count. */

    /**
     * Pool of pre-allocated ff_word_t structs for built-in words.
     * Replaces what used to be ~150 individual mallocs at startup
     * with a single block. Each pool entry carries FF_WORD_STATIC so
     * ff_word_free skips it (the block is freed wholesale by
     * ff_dict_destroy) and points its `name` straight at the
     * def-table string literal instead of strdup'ing.
     */
    ff_word_t *static_pool;
    size_t static_pool_size;
};

/* --- Helpers exported across word files for case bodies in ff_exec(). --- */

struct ff;

/**
 * Pretty-print a memory range as a hex+ASCII table.
 * @param ff   Engine instance (used for output).
 * @param addr First byte to dump.
 * @param size Number of bytes to dump.
 */
void ff_dump_bytes(struct ff *ff, const char *addr, size_t size);

#ifdef FF_OS_UNIX
/**
 * Emit a memory-status table read from /proc/self/statm.
 * @param ff Engine instance.
 */
void ff_print_memstat(struct ff *ff);
#endif

/**
 * List dictionary words, optionally filtered by use status.
 * @param ff     Engine instance.
 * @param filter 0 = all words, 1 = only used, 2 = only unused.
 */
void ff_print_words(struct ff *ff, int filter);

/**
 * Implementation of the `man` immediate word — print the manual entry
 * for the next-token word.
 * @param ff Engine instance.
 */
void ff_w_man_impl(struct ff *ff);

/**
 * Implementation of the `dump-word` introspection word — print the
 * raw heap of the next-token word.
 * @param ff Engine instance.
 */
void ff_w_dump_word_impl(struct ff *ff);

/**
 * Implementation of the `see` decompiler — render a Forth-syntax
 * approximation of the next-token word's body.
 * @param ff Engine instance.
 */
void ff_w_see_impl(struct ff *ff);

/* --- Built-in registration tables defined alongside their case bodies. --- */

extern const ff_word_def_t FF_ARRAY_WORDS[];   /**< Array words (`array`, `array-runtime`). */
extern const ff_word_def_t FF_COMP_WORDS[];    /**< Compile-time words (`:`, `;`, `'`, …). */
extern const ff_word_def_t FF_CONIO_WORDS[];   /**< Console I/O words (`.`, `cr`, `emit`, …). */
extern const ff_word_def_t FF_CTRL_WORDS[];    /**< Control flow (`if`, `do`, `loop`, …). */
extern const ff_word_def_t FF_DEBUG_WORDS[];   /**< Debug (`trace`, `dump`, `errno`, …). */
extern const ff_word_def_t FF_DICT_WORDS[];    /**< Introspection (`words`, `man`, `see`, …). */
extern const ff_word_def_t FF_EVAL_WORDS[];    /**< Evaluation (`evaluate`, `load`). */
extern const ff_word_def_t FF_FIELD_WORDS[];   /**< Word fields (`find`, `>name`, `>body`). */
extern const ff_word_def_t FF_FILE_WORDS[];    /**< File I/O (`fopen`, `fread`, …). */
extern const ff_word_def_t FF_HEAP_WORDS[];    /**< Heap (`@`, `!`, `,`, `here`, …). */
extern const ff_word_def_t FF_MATH_WORDS[];    /**< Integer math and comparisons. */
extern const ff_word_def_t FF_REAL_WORDS[];    /**< Floating-point math. */
extern const ff_word_def_t FF_STACK2_WORDS[];  /**< Double-cell stack ops (`2dup`, …). */
extern const ff_word_def_t FF_STACK_WORDS[];   /**< Stack ops (`dup`, `swap`, `pick`, …). */
extern const ff_word_def_t FF_STRING_WORDS[];  /**< String ops (`s!`, `s+`, `strlen`, …). */
extern const ff_word_def_t FF_VAR_WORDS[];     /**< Definitions (`create`, `variable`, …). */

/**
 * @brief Render a boolean as a UTF-8 checkmark or empty string.
 *
 * Used by tabular introspection output (`see`, `dump-word`).
 *
 * @param b Flag value.
 * @return "✓" when @p b is true, "" otherwise.
 */
static inline const char *ff_tick(bool b)
{
    return b
                ? "✓"
                : "";
}

/**
 * @brief Grow the words array if it can't hold @p extra more entries.
 *
 * @param d     Dictionary.
 * @param extra Slots needed beyond current @ref ff_dict::count.
 */
static void ff_dict_ensure(ff_dict_t *d, size_t extra);

/**
 * @brief Internal: register every built-in word into the static pool.
 *
 * @param d Dictionary, freshly initialized by @ref ff_dict_init.
 */
static void ff_dict_define_words(ff_dict_t *d);
