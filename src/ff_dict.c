/**
 * @file ff_dict.c
 * @brief Dictionary implementation: ordered word array, FNV-1a hash
 *        index, static-pool fast init, FORGET-driven truncation.
 */

#include "ff_dict_p.h"

#include "ff_word_p.h"

#include "utf8/utf8.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>


/** @brief Bucket count of the freshly-initialized hash table. */
#define FF_DICT_INITIAL_BUCKETS 256

/** @brief Default slab capacity for the dict's word-heap arena. */
#define FF_DICT_ARENA_SLAB     (64 * 1024)


/** @copydoc ff_arena_alloc */
void *ff_arena_alloc(ff_arena_t *a, size_t bytes)
{
    /* 8-byte alignment is enough for ff_int_t (intptr_t) on every
       platform we target. */
    bytes = (bytes + 7) & ~(size_t)7;

    ff_arena_slab_t *s = a->head;
    if (s == NULL || s->used + bytes > s->cap)
    {
        size_t cap = a->default_slab_size
                         ? a->default_slab_size
                         : FF_DICT_ARENA_SLAB;
        if (cap < bytes)
            cap = bytes;
        s = (ff_arena_slab_t *)malloc(sizeof(ff_arena_slab_t) + cap);
        s->cap  = cap;
        s->used = 0;
        s->next = a->head;
        a->head = s;
    }
    void *p = &s->data[s->used];
    s->used += bytes;
    return p;
}

/** @copydoc ff_arena_destroy */
void ff_arena_destroy(ff_arena_t *a)
{
    ff_arena_slab_t *s = a->head;
    while (s)
    {
        ff_arena_slab_t *n = s->next;
        free(s);
        s = n;
    }
    a->head = NULL;
}


/**
 * FNV-1a hash over ASCII-lowercase-folded bytes. Non-ASCII bytes
 * (>= 0x80) are passed through unchanged so the resulting hash matches
 * across "Foo"/"foo" for ASCII while staying byte-stable for any UTF-8
 * sequence; case-insensitive matching of non-ASCII characters happens
 * on the chain via @c utf8casecmp.
 *
 * @param name NUL-terminated word name.
 * @return 64-bit FNV-1a digest.
 */
static uint64_t ff_dict_hash(const char *name)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
    {
        unsigned char c = *p;
        if (c < 0x80 && c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

/**
 * Insert @p w at the head of its bucket. Newest-first wins on lookup,
 * which gives Forth's expected shadowing semantics.
 *
 * @param d Dictionary.
 * @param w Word to link in.
 */
static void ff_dict_bucket_insert(ff_dict_t *d, ff_word_t *w)
{
    size_t i = (size_t)(ff_dict_hash(w->name) & (d->bucket_count - 1));
    w->next_bucket = d->buckets[i];
    d->buckets[i] = w;
}

/**
 * Wipe every chain and reinsert each surviving word in append order
 * so the newest-wins property holds. Called by @ref ff_dict_forget
 * after the @ref ff_dict::words tail is truncated.
 *
 * @param d Dictionary.
 */
static void ff_dict_buckets_rebuild(ff_dict_t *d)
{
    memset(d->buckets, 0, d->bucket_count * sizeof(ff_word_t *));
    for (size_t i = 0; i < d->count; ++i)
        ff_dict_bucket_insert(d, d->words[i]);
}


// Public

/**
 * Walk a NULL-terminated def table and return its entry count.
 * @param defs Sentinel-terminated table.
 * @return Number of entries before the NULL sentinel.
 */
static size_t ff_word_def_count(const ff_word_def_t *defs)
{
    size_t n = 0;
    for (; defs->name; ++defs)
        ++n;
    return n;
}

/**
 * Sum of every built-in registration table. Hand-listed because the
 * tables live in independent translation units and the linker can't
 * iterate them for us.
 *
 * @return Total number of built-in words across all categories.
 */
static size_t ff_dict_builtin_count(void)
{
    return ff_word_def_count(FF_ARRAY_WORDS)
         + ff_word_def_count(FF_COMP_WORDS)
         + ff_word_def_count(FF_CONIO_WORDS)
         + ff_word_def_count(FF_CTRL_WORDS)
         + ff_word_def_count(FF_DEBUG_WORDS)
         + ff_word_def_count(FF_DICT_WORDS)
         + ff_word_def_count(FF_EVAL_WORDS)
         + ff_word_def_count(FF_FIELD_WORDS)
         + ff_word_def_count(FF_FILE_WORDS)
         + ff_word_def_count(FF_HEAP_WORDS)
         + ff_word_def_count(FF_MATH_WORDS)
         + ff_word_def_count(FF_REAL_WORDS)
         + ff_word_def_count(FF_STACK2_WORDS)
         + ff_word_def_count(FF_STACK_WORDS)
         + ff_word_def_count(FF_STRING_WORDS)
         + ff_word_def_count(FF_VAR_WORDS);
}

/** @copydoc ff_dict_init */
void ff_dict_init(ff_dict_t *d, const ff_builtins_t *builtins)
{
    memset(d, 0, sizeof(*d));
    d->bucket_count = FF_DICT_INITIAL_BUCKETS;
    d->buckets = (ff_word_t **)calloc(d->bucket_count, sizeof(ff_word_t *));
    d->builtins = builtins;
    if (builtins && builtins->static_pool_size)
        d->builtins_used = (uint8_t *)calloc((builtins->static_pool_size + 7) / 8, 1);
}

/** @copydoc ff_dict_destroy */
void ff_dict_destroy(ff_dict_t *d)
{
    for (size_t i = 0; i < d->count; ++i)
        ff_word_free(d->words[i]);
    ff_arena_destroy(&d->arena);
    free(d->words);
    free(d->buckets);
    free(d->builtins_used);
    free(d->intervals);
    memset(d, 0, sizeof(*d));
}

/** @copydoc ff_dict_top */
ff_word_t *ff_dict_top(ff_dict_t *d)
{
    return d->count
                ? d->words[d->count - 1]
                : NULL;
}

/* Compute the index of @p w within the shared static_pool, or
   SIZE_MAX if @p w isn't a member. Used to gate the per-instance
   "used" bitmap and to detect "is this a built-in?". */
static size_t ff_dict_builtin_index(const ff_dict_t *d, const ff_word_t *w)
{
    if (!d->builtins || !w)
        return (size_t)-1;
    const ff_word_t *base = d->builtins->static_pool;
    if (w < base || w >= base + d->builtins->static_pool_size)
        return (size_t)-1;
    return (size_t)(w - base);
}

/** @copydoc ff_dict_lookup */
ff_word_t *ff_dict_lookup(ff_dict_t *d, const char *name)
{
    size_t hash = (size_t)ff_dict_hash(name);

    /* User words first — they shadow built-ins per Forth tradition. */
    size_t i = hash & (d->bucket_count - 1);
    for (ff_word_t *w = d->buckets[i]; w; w = w->next_bucket)
    {
        if (utf8casecmp(w->name, name) == 0)
        {
            w->flags |= FF_WORD_USED;
            return w;
        }
    }

    /* Fall through to shared built-ins. The pool is read-only across
       instances, so the USED bit is recorded in this dict's bitmap
       instead of being written into the shared word's flags. */
    if (d->builtins)
    {
        size_t bi = hash & (d->builtins->bucket_count - 1);
        for (ff_word_t *w = d->builtins->buckets[bi]; w; w = w->next_bucket)
        {
            if (utf8casecmp(w->name, name) == 0)
            {
                size_t pi = ff_dict_builtin_index(d, w);
                if (pi != (size_t)-1 && d->builtins_used)
                    d->builtins_used[pi >> 3] |= (uint8_t)(1u << (pi & 7));
                return w;
            }
        }
    }

    return NULL;
}

size_t ff_dict_total_count(const ff_dict_t *d)
{
    return d->count + (d->builtins ? d->builtins->static_pool_size : 0);
}

const ff_word_t *ff_dict_word_at(const ff_dict_t *d, size_t i)
{
    if (i < d->count)
        return d->words[i];
    if (!d->builtins)
        return NULL;
    size_t bi = i - d->count;
    if (bi >= d->builtins->static_pool_size)
        return NULL;
    return &d->builtins->static_pool[bi];
}

bool ff_dict_word_was_used(const ff_dict_t *d, const ff_word_t *w)
{
    size_t pi = ff_dict_builtin_index(d, w);
    if (pi != (size_t)-1)
    {
        if (!d->builtins_used)
            return false;
        return (d->builtins_used[pi >> 3] >> (pi & 7)) & 1;
    }
    return (w->flags & FF_WORD_USED) != 0;
}

/** @copydoc ff_dict_append */
ff_word_t *ff_dict_append(ff_dict_t *d, ff_word_t *w)
{
    assert(w);

    ff_dict_ensure(d, 1);
    d->words[d->count++] = w;
    ff_dict_bucket_insert(d, w);
    /* Wire the heap to bump our mutation_seq on every realloc-that-
       moves-data, then bump for this append itself. */
    w->heap.mutation_seq_p = &d->mutation_seq;
    /* Bind the heap to the dict's arena unless the heap already owns
       a malloc'd buffer (native-word fn pointer stashed at heap.data[0]
       during ff_word_init_common, before this append runs). Mixing
       arena and malloc on the same heap would risk freeing arena
       memory in ff_heap_destroy or vice versa. */
    if (w->heap.data == NULL)
        w->heap.arena = &d->arena;
    ++d->mutation_seq;
    return w;
}

/** @copydoc ff_dict_rename */
void ff_dict_rename(ff_dict_t *d, ff_word_t *w, const char *new_name)
{
    assert(w && new_name);
    /* Rename only ever fires for placeholder colon-defs (`:`, `variable`,
       `constant`, `array`, `string`) created via ff_word_new. Built-ins
       in the static pool never reach this path. */
    assert(!(w->flags & FF_WORD_STATIC));

    /* Unlink from the current bucket (keyed on the old name). */
    size_t i = (size_t)(ff_dict_hash(w->name) & (d->bucket_count - 1));
    ff_word_t **link = &d->buckets[i];
    while (*link && *link != w)
        link = &(*link)->next_bucket;
    if (*link == w)
        *link = w->next_bucket;

    /* Replace name and reinsert into the bucket of the new name. */
    free(w->name);
    w->name = strdup(new_name);
    ff_dict_bucket_insert(d, w);
}

/** @copydoc ff_dict_forget */
bool ff_dict_forget(ff_dict_t *d, const char *name)
{
    /* Forget only walks user words. Built-ins live in the shared
       block — forgetting one would mutate state seen by every other
       engine sharing the singleton, so callers get FF_ERR_FORGET_PROT
       (returned as `false` here, with the public error path raising
       FF_ERR_UNDEFINED today; tightening the message is a follow-up). */
    for (int i = (int)d->count - 1; i >= 0; --i)
    {
        if (utf8casecmp(d->words[i]->name, name) == 0)
        {
            for (size_t j = i; j < d->count; ++j)
                ff_word_free(d->words[j]);
            d->count = (size_t)i;
            ff_dict_buckets_rebuild(d);
            ++d->mutation_seq;
            return true;
        }
    }
    return false;
}

/* qsort comparator: ascending by interval `lo`. */
static int ff_interval_cmp(const void *a, const void *b)
{
    const ff_interval_t *ia = (const ff_interval_t *)a;
    const ff_interval_t *ib = (const ff_interval_t *)b;
    if (ia->lo < ib->lo) return -1;
    if (ia->lo > ib->lo) return  1;
    return 0;
}

/** @copydoc ff_dict_intervals */
const ff_interval_t *ff_dict_intervals(ff_dict_t *d, size_t *count)
{
    if (d->intervals_built_at == d->mutation_seq && d->intervals)
    {
        *count = d->intervals_count;
        return d->intervals;
    }

    /* Rebuild from scratch: capacity-and-up-from-here. Re-sized once
       per mutation rather than per word, which dominates the cost. */
    if (d->intervals_capacity < d->count)
    {
        size_t nc = d->intervals_capacity ? d->intervals_capacity : 64;
        while (nc < d->count) nc *= 2;
        d->intervals = (ff_interval_t *)realloc(d->intervals,
                                                nc * sizeof(ff_interval_t));
        d->intervals_capacity = nc;
    }

    size_t n = 0;
    for (size_t i = 0; i < d->count; ++i)
    {
        const ff_word_t *w = d->words[i];
        if (!w || !w->heap.data || w->heap.capacity == 0)
            continue;
        const char *lo = (const char *)w->heap.data;
        const char *hi = lo + w->heap.capacity * sizeof(ff_int_t);
        d->intervals[n].lo = lo;
        d->intervals[n].hi = hi;
        ++n;
    }
    if (n > 1)
        qsort(d->intervals, n, sizeof(ff_interval_t), ff_interval_cmp);

    d->intervals_count = n;
    d->intervals_built_at = d->mutation_seq;
    *count = n;
    return d->intervals;
}

/** @copydoc ff_dict_define */
void ff_dict_define(ff_dict_t *d, const ff_word_def_t *defs)
{
    for (const ff_word_def_t *def = defs; def->name; ++def)
        ff_dict_append(d,
                       def->is_immediate
                            ? ff_im_word_new(def->name, def->code, def->opcode, def->manual)
                            : ff_word_new(def->name, def->code, def->opcode, def->manual));
}

// Private

/**
 * Grow @ref ff_dict::words to fit @p extra additional entries,
 * doubling capacity as needed.
 *
 * @param d     Dictionary.
 * @param extra Slots required beyond @ref ff_dict::count.
 */
void ff_dict_ensure(ff_dict_t *d, size_t extra)
{
    if (d->count + extra > d->capacity)
    {
        size_t nc = d->capacity ? d->capacity * 2 : 128;
        while (nc < d->count + extra)
            nc *= 2;
        d->words = (ff_word_t **)realloc(d->words, nc * sizeof(ff_word_t *));
        d->capacity = nc;
    }
}


/* ===================================================================
 * Shared built-in registration.
 * =================================================================== */

/* Hash-bucket insert that targets a generic bucket array (used by
   ff_builtins_init, where the buckets aren't on a ff_dict). */
static void ff_builtins_bucket_insert(ff_word_t **buckets, size_t bcount,
                                      ff_word_t *w)
{
    size_t i = (size_t)(ff_dict_hash(w->name) & (bcount - 1));
    w->next_bucket = buckets[i];
    buckets[i] = w;
}

static void ff_builtins_define_static(ff_builtins_t *b, const ff_word_def_t *defs,
                                      size_t *pool_idx)
{
    for (const ff_word_def_t *def = defs; def->name; ++def)
    {
        assert(*pool_idx < b->static_pool_size);
        ff_word_t *w = &b->static_pool[(*pool_idx)++];
        ff_word_init_static(w, def->name, def->code, def->opcode, def->manual);
        if (def->is_immediate)
            w->flags |= FF_WORD_IMMEDIATE;
        ff_builtins_bucket_insert(b->buckets, b->bucket_count, w);
    }
}

/** @copydoc ff_builtins_init */
void ff_builtins_init(ff_builtins_t *b)
{
    memset(b, 0, sizeof(*b));
    b->static_pool_size = ff_dict_builtin_count();
    b->static_pool = (ff_word_t *)calloc(b->static_pool_size, sizeof(ff_word_t));
    b->bucket_count = FF_DICT_INITIAL_BUCKETS;
    b->buckets = (ff_word_t **)calloc(b->bucket_count, sizeof(ff_word_t *));

    size_t pool_idx = 0;
    ff_builtins_define_static(b, FF_ARRAY_WORDS,  &pool_idx);
    ff_builtins_define_static(b, FF_COMP_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_CONIO_WORDS,  &pool_idx);
    ff_builtins_define_static(b, FF_CTRL_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_DEBUG_WORDS,  &pool_idx);
    ff_builtins_define_static(b, FF_DICT_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_EVAL_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_FIELD_WORDS,  &pool_idx);
    ff_builtins_define_static(b, FF_FILE_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_HEAP_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_MATH_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_REAL_WORDS,   &pool_idx);
    ff_builtins_define_static(b, FF_STACK2_WORDS, &pool_idx);
    ff_builtins_define_static(b, FF_STACK_WORDS,  &pool_idx);
    ff_builtins_define_static(b, FF_STRING_WORDS, &pool_idx);
    ff_builtins_define_static(b, FF_VAR_WORDS,    &pool_idx);
    assert(pool_idx == b->static_pool_size);

    /* Sorted intervals over native-word fn-pointer heaps. Built-ins
       with FF_WORD_NATIVE stash a fn pointer at heap.data[0] (one
       cell of malloc'd storage). User code that does `c@` / `@`
       through one of those pointers needs ff_addr_valid to recognize
       it under FF_SAFE_MEM. */
    size_t native_count = 0;
    for (size_t i = 0; i < b->static_pool_size; ++i)
        if (b->static_pool[i].heap.data && b->static_pool[i].heap.capacity)
            ++native_count;
    if (native_count)
    {
        b->intervals = (ff_interval_t *)calloc(native_count, sizeof(ff_interval_t));
        size_t n = 0;
        for (size_t i = 0; i < b->static_pool_size; ++i)
        {
            const ff_word_t *w = &b->static_pool[i];
            if (!w->heap.data || !w->heap.capacity) continue;
            b->intervals[n].lo = (const char *)w->heap.data;
            b->intervals[n].hi = (const char *)w->heap.data
                                  + w->heap.capacity * sizeof(ff_int_t);
            ++n;
        }
        if (n > 1)
            qsort(b->intervals, n, sizeof(ff_interval_t), ff_interval_cmp);
        b->intervals_count = n;
    }
}

/** @copydoc ff_builtins_destroy */
void ff_builtins_destroy(ff_builtins_t *b)
{
    if (!b) return;
    /* Each native built-in's heap.data is a single-cell malloc holding
       the fn pointer. Free those individually before releasing the
       pool itself. Static names point at string literals — no free. */
    for (size_t i = 0; i < b->static_pool_size; ++i)
        ff_heap_destroy(&b->static_pool[i].heap);
    free(b->static_pool);
    free(b->buckets);
    free(b->intervals);
    memset(b, 0, sizeof(*b));
}


/* ===================================================================
 * Process-wide singleton, lazily initialised on first ff_new.
 *
 * Thread-safety: `ff_builtins_default()` itself is not thread-safe on
 * its first call (see the spinning compare-exchange). Embedders that
 * spin up engine instances from multiple threads concurrently should
 * call `ff_builtins_default()` once from the main thread first, or
 * use `ff_builtins_init` on a host-owned struct instead.
 * =================================================================== */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L \
        && !defined(__STDC_NO_ATOMICS__)
#  include <stdatomic.h>
static atomic_int g_builtins_state;   /* 0=uninit, 1=initing, 2=ready */
#else
static volatile int g_builtins_state;
#endif
static ff_builtins_t g_builtins;

const ff_builtins_t *ff_builtins_default(void)
{
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L \
        && !defined(__STDC_NO_ATOMICS__)
    int s = atomic_load_explicit(&g_builtins_state, memory_order_acquire);
    if (s == 2)
        return &g_builtins;
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_builtins_state, &expected, 1))
    {
        ff_builtins_init(&g_builtins);
        atomic_store_explicit(&g_builtins_state, 2, memory_order_release);
    }
    else
    {
        while (atomic_load_explicit(&g_builtins_state, memory_order_acquire) != 2)
            ; /* brief spin until the racing initializer flips state to 2 */
    }
#else
    if (g_builtins_state != 2)
    {
        if (g_builtins_state == 0)
        {
            g_builtins_state = 1;
            ff_builtins_init(&g_builtins);
            g_builtins_state = 2;
        }
        else
        {
            while (g_builtins_state != 2)
                ;
        }
    }
#endif
    return &g_builtins;
}
