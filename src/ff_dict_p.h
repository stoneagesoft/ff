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
typedef struct ff_builtins ff_builtins_t;

/**
 * Initialize a per-instance dictionary that delegates built-in lookups
 * to the shared @p builtins block.
 *
 * @param d        Dictionary to initialize.
 * @param builtins Shared built-in block, typically @ref ff_builtins_default().
 */
void ff_dict_init(ff_dict_t *d, const ff_builtins_t *builtins);

/** @brief Total word count: user words + shared built-ins. */
size_t ff_dict_total_count(const ff_dict_t *d);

/**
 * @brief Return the @p i-th word across the merged scope.
 *
 * Indices [0, user_count) refer to user words in append order;
 * [user_count, total) refer to built-ins in their static-pool order.
 */
const ff_word_t *ff_dict_word_at(const ff_dict_t *d, size_t i);

/**
 * @brief True if @p w has been looked up at least once on this engine.
 *
 * For user words this just reads the FF_WORD_USED flag. For shared
 * built-ins it consults the per-instance @ref ff_dict::builtins_used
 * bitmap, since the shared word's flags are immutable.
 */
bool ff_dict_word_was_used(const ff_dict_t *d, const ff_word_t *w);

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
 * @brief One [lo, hi) range in the dict's sorted interval index.
 *
 * Sorted by `lo`. Used by @ref ff_addr_valid to decide membership in
 * O(log N) instead of linearly scanning every word's heap.
 */
typedef struct ff_interval
{
    const char *lo;        /**< First byte (inclusive). */
    const char *hi;        /**< One past the last byte. */
} ff_interval_t;

/**
 * @brief One slab in the dict's word-heap arena.
 *
 * Bumps `used` forward as words allocate. When the next allocation
 * doesn't fit, a new slab is linked in. Slabs are freed in bulk by
 * @ref ff_dict_destroy.
 */
typedef struct ff_arena_slab
{
    struct ff_arena_slab *next; /**< Linked list, newest first. */
    size_t cap;                  /**< Bytes in @ref data. */
    size_t used;                 /**< Bytes consumed. */
    char data[];                 /**< Flexible payload. */
} ff_arena_slab_t;

/**
 * @struct ff_arena
 * @brief Slab arena dispensing word-heap allocations.
 *
 * Replaces N individual mallocs (one per ff_word_t::heap) with a few
 * O(N / slab_size) slab mallocs. Allocations are bump-pointer; growth
 * via @ref ff_heap_grow allocates a fresh region and abandons the
 * old one. Trades a bit of internal fragmentation (wasted tails of
 * grown-and-relocated heaps) for a large reduction in allocator
 * traffic when the dictionary is heavy with small definitions.
 */
struct ff_arena
{
    ff_arena_slab_t *head;       /**< Newest slab; allocations come from here first. */
    size_t           default_slab_size; /**< Default `cap` for new slabs. */
};

/** @brief Allocate @p bytes from the arena. Never returns NULL. */
void *ff_arena_alloc(ff_arena_t *a, size_t bytes);

/** @brief Free every slab. The arena is left zeroed. */
void  ff_arena_destroy(ff_arena_t *a);


/**
 * @struct ff_builtins
 * @brief Process-wide, read-only registration of every built-in word.
 *
 * Initialised once (lazily on first @ref ff_new) and reused across
 * every per-instance dictionary. Sharing this block instead of
 * copying it into each engine's static pool turns N × 14 KB of
 * repeated word structs into a single 14 KB allocation, plus one
 * 2 KB shared bucket array.
 *
 * The built-ins themselves are immutable after init: no `next_bucket`
 * shuffle, no FF_WORD_USED writes (that bit is tracked per-instance
 * in @ref ff_dict::builtins_used). Forgetting a built-in is rejected
 * with FF_ERR_FORGET_PROT — preserving them across instances would
 * require per-instance shadow state we don't currently keep.
 */
struct ff_builtins
{
    ff_word_t  *static_pool;       /**< Contiguous pool of built-in word structs. */
    size_t      static_pool_size;  /**< Number of valid entries in @ref static_pool. */
    ff_word_t **buckets;           /**< Power-of-two hash buckets over the pool. */
    size_t      bucket_count;
    ff_interval_t *intervals;      /**< Sorted heap intervals (native-word fn-pointer heaps). */
    size_t         intervals_count;
};

/** @brief Populate @p b with every FF_*_WORDS table; thread-unsafe. */
void ff_builtins_init(ff_builtins_t *b);
/** @brief Free everything @ref ff_builtins_init allocated. */
void ff_builtins_destroy(ff_builtins_t *b);

/** @brief Process-wide singleton, lazily initialised by @ref ff_new. */
const ff_builtins_t *ff_builtins_default(void);

/**
 * @struct ff_dict
 * @brief Per-instance dictionary holding user-defined words; built-in
 *        words live in the shared @ref ff_builtins block referenced
 *        by @ref builtins.
 */
struct ff_dict
{
    /**
     * Ordered array of user-defined words (newest at end). Built-in
     * words are not stored here — see @ref builtins.
     */
    ff_word_t **words;
    size_t count;          /**< Number of valid user words. */
    size_t capacity;       /**< Allocated length of @ref words. */

    /**
     * Hash buckets for user words. Lookup falls through to the shared
     * builtins' buckets after missing here, giving Forth's expected
     * "user redefinitions shadow built-ins" semantics.
     */
    ff_word_t **buckets;
    size_t bucket_count;

    /** @brief Reference to the shared, read-only built-in block. */
    const ff_builtins_t *builtins;

    /**
     * Per-instance bitmap recording which shared built-ins this engine
     * has touched. Indexed by position within @ref ff_builtins::static_pool.
     * Replaces the FF_WORD_USED flag write on shared words (which would
     * race across instances).
     */
    uint8_t *builtins_used;

    /**
     * Mutation sequence — bumped on append, forget, and any
     * heap-realloc that moves a tracked word's data. The interval
     * index below is rebuilt lazily when @ref intervals_built_at
     * differs.
     */
    unsigned long mutation_seq;

    /**
     * Sorted interval index over each word's heap. Built lazily by
     * @ref ff_dict_intervals on first call after a mutation; cached
     * across queries. @ref ff_addr_valid binary-searches it.
     */
    ff_interval_t *intervals;
    size_t intervals_count;
    size_t intervals_capacity;
    unsigned long intervals_built_at;

    /**
     * Slab arena for word heaps. New non-native words bind their
     * heap to this arena at append time; the arena's lifetime is
     * the dict's. Native words (FF_WORD_NATIVE) keep the legacy
     * malloc path so their pre-arena fn-pointer stash stays valid.
     */
    ff_arena_t arena;
};

/**
 * Refresh and return the sorted interval index, rebuilding from the
 * current heap state if @ref ff_dict::mutation_seq has advanced past
 * @ref ff_dict::intervals_built_at.
 *
 * @param d     Dictionary.
 * @param count Out — number of valid entries in the returned array.
 * @return Sorted-by-lo array of intervals; valid until the next dict
 *         mutation.
 */
const ff_interval_t *ff_dict_intervals(ff_dict_t *d, size_t *count);

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
