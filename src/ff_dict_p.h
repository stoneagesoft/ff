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

#include <ff_heap_p.h>
#include <ff_mem_p.h>
#include <ff_opcode_p.h>
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
 * @brief True if @p w is one of the shared built-in words: it lies in
 *        the built-in pool *and* at the start of a word there. @p w may
 *        be any value a program passed as an xt.
 */
bool ff_dict_is_builtin(const ff_dict_t *d, const ff_word_t *w);

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
 * Append @p w as the newest word and link it into its hash bucket, and
 * charge it to the dictionary's account.
 *
 * @param d Dictionary.
 * @param w Word to append (may be NULL, from a failed ff_word_new());
 *          must outlive @p d unless freed via ff_dict_truncate() /
 *          ff_dict_remove() / ff_dict_destroy().
 * @return @p w, or NULL — with @p w freed — if the word table couldn't
 *         grow.
 */
ff_word_t *ff_dict_append(ff_dict_t *d, ff_word_t *w);

/**
 * @brief What a user word named @p name costs the account: the word, its
 *        name and its slot in the word table (its heap is charged to
 *        the arena separately).
 */
size_t ff_dict_word_cost(const char *name);

/**
 * @param d    Dictionary.
 * @param name Name to find, case-insensitively.
 * @return Index in @ref ff_dict::words of the newest user word named
 *         @p name, or (size_t)-1 (built-ins are never found).
 */
size_t ff_dict_index(const ff_dict_t *d, const char *name);

/**
 * Remove the user word at @p index and every later-defined one (Forth's
 * FORGET, once ff_dict_index() has found the word). Rebuilds the bucket
 * index, and hands the arena back from where the removed words began.
 *
 * @param d     Dictionary.
 * @param index Index in @ref ff_dict::words; out of range is a no-op.
 */
void ff_dict_truncate(ff_dict_t *d, size_t index);

/**
 * Remove the single word @p w from the dictionary and free it, leaving
 * every other word in place — unlike ff_dict_truncate(), which cuts off
 * everything defined after it too. Used to drop a definition that failed
 * to compile: nothing compiled before it can refer to it. The arena is
 * handed back from where @p w began, short of any remaining word's heap.
 *
 * @param d Dictionary.
 * @param w Word to remove.
 * @return false if @p w is not a user word in @p d.
 */
bool ff_dict_remove(ff_dict_t *d, ff_word_t *w);

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
 * @brief One live word heap in the arena: the [lo, hi) range of its
 *        capacity, and whose heap it is.
 *
 * The arena keeps these sorted by `lo` as heaps are allocated, grown,
 * trimmed and freed, for @ref ff_addr_valid to find the heap containing
 * an address in O(log N). Addresses are compared as integers.
 */
typedef struct ff_region
{
    uintptr_t        lo;    /**< First byte (inclusive). */
    uintptr_t        hi;    /**< One past the heap's capacity. */
    const ff_heap_t *owner; /**< The heap: bytecode can be read but not written, a data word's
                                 heap both (see ff_word_holds_data()). */
} ff_region_t;

/**
 * @brief One slab in the dict's word-heap arena.
 *
 * Bumps `used` forward as words allocate. When the next allocation
 * doesn't fit, a new slab is linked in. Removing words hands back the
 * slabs allocated after them (see ff_dict_truncate()); the rest are freed
 * by @ref ff_dict_destroy.
 */
typedef struct ff_arena_slab
{
    struct ff_arena_slab *next; /**< Linked list, newest first. */
    unsigned long seq;           /**< Creation order, from 1; orders ff_arena_mark_t positions. */
    size_t cap;                  /**< Bytes in @ref data. */
    size_t used;                 /**< Bytes consumed. */
    char data[];                 /**< Flexible payload. */
} ff_arena_slab_t;

/**
 * @struct ff_arena
 * @brief Slab arena dispensing word-heap allocations.
 *
 * Replaces N individual mallocs (one per ff_word_t::heap) with a few
 * O(N / slab_size) slab mallocs. Allocations are bump-pointer. A heap
 * grows where it lies when it is the newest allocation — the usual case
 * for the definition being compiled or a data word being filled — and is
 * otherwise moved to a fresh region, abandoning the old one (see
 * ff_arena_heap_grow()).
 */
struct ff_arena
{
    ff_arena_slab_t *head;       /**< Newest slab; allocations come from here first. */
    size_t           default_slab_size; /**< Default `cap` for new slabs. */
    unsigned long    seq;        /**< Sequence number of the newest slab ever made. */
    ff_mem_t        *mem;        /**< Account slabs are charged to, or NULL. */
    ff_region_t     *regions;    /**< Every live heap's region, sorted by address. */
    size_t           n_regions;  /**< Entries in @ref regions. */
    size_t           cap_regions; /**< Allocated length of @ref regions. */
};

/**
 * @brief Allocate @p bytes from the arena.
 *
 * A new slab counts against the account's limit. When the limit or the
 * host allocator refuses it, the refusal is recorded in the account and
 * NULL is returned; the caller writes nothing, and the engine raises the
 * error at its next check (see ff_mem_p.h).
 */
void *ff_arena_alloc(ff_arena_t *a, size_t bytes);

/** @brief Whether an allocation of @p bytes would stay within the limit. */
bool  ff_arena_fits(const ff_arena_t *a, size_t bytes);

/** @brief Record a refused allocation of @p bytes (a size that can't be represented). */
void  ff_arena_refuse(ff_arena_t *a, size_t bytes);

/** @brief The arena's current position: where the next allocation starts. */
ff_arena_mark_t ff_arena_mark(const ff_arena_t *a);

/** @brief Free every slab. The arena is left zeroed. */
void  ff_arena_destroy(ff_arena_t *a);

/**
 * @brief Give heap @p h room for at least @p need cells, @p want if the
 *        limit allows.
 *
 * The heap grows where it lies when it is the arena's newest allocation:
 * into its slab's free tail, or, when it is the slab's only region, by
 * reallocating the slab. Otherwise it moves to a fresh region, its live
 * cells copied over. Either way the region index and ff_heap::end follow.
 *
 * @return false, leaving the heap as it was and the refusal recorded in
 *         the account, if the limit or the allocator refused it.
 */
bool  ff_arena_heap_grow(ff_arena_t *a, ff_heap_t *h, size_t want, size_t need);

/**
 * @brief Shrink heap @p h's capacity to its size. The arena takes the
 *        freed tail back if the heap is its newest allocation.
 */
void  ff_arena_heap_trim(ff_arena_t *a, ff_heap_t *h);

/** @brief Heap @p h is being freed: drop its region from the index. */
void  ff_arena_heap_drop(ff_arena_t *a, const ff_heap_t *h);


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
    /** The built-in word each opcode runs as (the first registered), or
        NULL for internal opcodes with no word — for naming an opcode, as
        `see` does. */
    const ff_word_t *by_opcode[FF_OP_COUNT];
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
    size_t capacity;       /**< Allocated length of @ref words and @ref by_addr. */

    /**
     * The same words sorted by address, so an xt can be checked in
     * O(log N) (see ff_dict_contains()).
     */
    ff_word_t **by_addr;

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
     * Slab arena for word heaps. Every word binds its heap to this
     * arena when it joins the dictionary; the arena's lifetime is the
     * dict's, and it indexes the heaps' regions for ff_addr_valid().
     */
    ff_arena_t arena;

    /**
     * What this engine holds for Forth code — the arena's slabs, the
     * user words and their names, and (charged by the engine) the
     * transient string arena — against the host's limit.
     */
    ff_mem_t mem;
};

/**
 * @param d    Dictionary.
 * @param addr Any address.
 * @return The region of the word heap containing @p addr, or NULL.
 *         Valid until the dictionary next changes.
 */
const ff_region_t *ff_dict_region_at(const ff_dict_t *d, const void *addr);

/**
 * @param d Dictionary.
 * @param w Any pointer, as a program may pass for an xt.
 * @return true if @p w is one of the dictionary's user words.
 */
bool ff_dict_contains(const ff_dict_t *d, const ff_word_t *w);

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
