/**
 * @file ff_dict.c
 * @brief Dictionary implementation: ordered word array, FNV-1a hash
 *        index, static-pool fast init, FORGET-driven truncation.
 */

#include "ff_dict_p.h"

#include "ff_word_p.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>


/** @brief Bucket count of the freshly-initialized hash table. */
#define FF_DICT_INITIAL_BUCKETS 256

/** @brief Default slab capacity for the dict's word-heap arena. */
#define FF_DICT_ARENA_SLAB     (64 * 1024)


static bool ff_dict_ensure(ff_dict_t *d, size_t extra);


/** @copydoc ff_arena_refuse */
void ff_arena_refuse(ff_arena_t *a, size_t bytes)
{
    if (a->mem)
        ff_mem_refuse(a->mem, bytes, false);
}

/* Capacity of the slab a request for `bytes` (already rounded) would
   open: the default size, or the request if bigger — cut back to what
   the limit leaves, since a request that fits shouldn't fail for the
   sake of the slack after it. 0 if even the request doesn't fit. */
static size_t ff_arena_slab_cap(const ff_arena_t *a, size_t bytes)
{
    size_t cap = a->default_slab_size ? a->default_slab_size
                                      : FF_DICT_ARENA_SLAB;
    if (cap < bytes)
        cap = bytes;
    if (a->mem)
    {
        size_t room = ff_mem_room(a->mem);
        size_t head = sizeof(ff_arena_slab_t);
        if (room < head + bytes)
            return 0;
        if (cap > room - head)
            cap = room - head;
    }
    return cap;
}

/** @copydoc ff_arena_fits */
bool ff_arena_fits(const ff_arena_t *a, size_t bytes)
{
    if (bytes > SIZE_MAX - 7)
        return false;
    bytes = (bytes + 7) & ~(size_t)7;
    const ff_arena_slab_t *s = a->head;
    if (s && bytes <= s->cap - s->used)
        return true;
    return bytes <= SIZE_MAX - sizeof(ff_arena_slab_t)
               && ff_arena_slab_cap(a, bytes) != 0;
}

/** @copydoc ff_arena_alloc */
void *ff_arena_alloc(ff_arena_t *a, size_t bytes)
{
    if (bytes > SIZE_MAX - 7 - sizeof(ff_arena_slab_t))
    {
        ff_arena_refuse(a, SIZE_MAX);
        return NULL;
    }
    /* 8-byte alignment is enough for ff_int_t (intptr_t) on every
       platform we target. */
    bytes = (bytes + 7) & ~(size_t)7;

    ff_arena_slab_t *s = a->head;
    if (s == NULL || bytes > s->cap - s->used)
    {
        size_t cap = ff_arena_slab_cap(a, bytes);
        if (cap == 0)
        {
            ff_arena_refuse(a, bytes);
            return NULL;
        }
        s = (ff_arena_slab_t *)malloc(sizeof(ff_arena_slab_t) + cap);
        if (!s)
        {
            if (a->mem)
                ff_mem_refuse(a->mem, bytes, true);
            return NULL;
        }
        s->cap  = cap;
        s->used = 0;
        s->seq  = ++a->seq;
        s->next = a->head;
        a->head = s;
        if (a->mem)
            ff_mem_charge(a->mem, sizeof(ff_arena_slab_t) + cap);
    }
    void *p = &s->data[s->used];
    s->used += bytes;
    return p;
}

/** @copydoc ff_arena_mark */
ff_arena_mark_t ff_arena_mark(const ff_arena_t *a)
{
    ff_arena_mark_t m = { 0, 0 };
    if (a->head)
    {
        m.seq  = a->head->seq;
        m.used = a->head->used;
    }
    return m;
}

/* True if position `x` lies after position `y`. */
static bool ff_arena_after(ff_arena_mark_t x, ff_arena_mark_t y)
{
    return x.seq > y.seq || (x.seq == y.seq && x.used > y.used);
}

/* Position just past the region of `bytes` bytes that starts at `p`, if
   `p` is in one of the arena's slabs. Located by its start: an end
   address can coincide with the start of another slab. */
static bool ff_arena_locate_end(const ff_arena_t *a, const void *p,
                                size_t bytes, ff_arena_mark_t *out)
{
    const char *c = (const char *)p;
    for (const ff_arena_slab_t *s = a->head; s; s = s->next)
    {
        if (c >= s->data && c < s->data + s->cap)
        {
            out->seq  = s->seq;
            out->used = (size_t)(c - s->data) + ((bytes + 7) & ~(size_t)7);
            return true;
        }
    }
    return false;
}

/* Give back everything allocated after position `m`: free the slabs
   made since, and rewind the one `m` is in. */
static void ff_arena_release(ff_arena_t *a, ff_arena_mark_t m)
{
    while (a->head && a->head->seq > m.seq)
    {
        ff_arena_slab_t *s = a->head;
        a->head = s->next;
        if (a->mem)
            ff_mem_release(a->mem, sizeof(ff_arena_slab_t) + s->cap);
        free(s);
    }
    if (a->head && a->head->seq == m.seq && a->head->used > m.used)
        a->head->used = m.used;
}

/* Round a byte count up the same way ff_arena_alloc does, so trim
   measurements line up with what alloc actually consumed. */
static size_t ff_arena_round(size_t bytes)
{
    return (bytes + 7) & ~(size_t)7;
}

/* Try to shrink @p region from @p old_bytes to @p new_bytes. Effective
   only when @p region is at the tail of the current slab — otherwise
   this is a no-op (the freed space is sandwiched between live
   regions and can't be reclaimed without compaction). */
void ff_arena_trim(ff_arena_t *a, void *region, size_t old_bytes,
                   size_t new_bytes)
{
    if (!a || !a->head || !region) return;
    if (new_bytes >= old_bytes) return;
    ff_arena_slab_t *s = a->head;
    size_t old_aligned = ff_arena_round(old_bytes);
    size_t new_aligned = ff_arena_round(new_bytes);
    char *region_end = (char *)region + old_aligned;
    if (region_end != &s->data[s->used])
        return;     /* not at the tail — can't reclaim */
    s->used -= (old_aligned - new_aligned);
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
 * Fold an ASCII letter to lower case; any other byte is its own fold.
 *
 * @param c Byte of a word name.
 * @return The byte names are hashed and compared by.
 */
static inline unsigned char ff_dict_fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c;
}

/**
 * FNV-1a hash over ASCII-lowercase-folded bytes. Non-ASCII bytes
 * (>= 0x80) are passed through unchanged, so "Foo" and "foo" hash alike
 * and any UTF-8 sequence hashes byte for byte.
 *
 * @param name NUL-terminated word name.
 * @return 64-bit FNV-1a digest.
 */
static uint64_t ff_dict_hash(const char *name)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
    {
        h ^= ff_dict_fold(*p);
        h *= 0x100000001b3ULL;
    }
    return h;
}

/**
 * Whether two word names match: ASCII letters regardless of case, every
 * other byte exactly — the same folding as ff_dict_hash(), so a name is
 * found in exactly the bucket it hashes to. (utf8casecmp(), used before,
 * folded non-ASCII letters too, which only matched when both spellings
 * happened to share a bucket, and read past the end of a name ending in
 * a cut-off UTF-8 sequence.)
 *
 * @param a NUL-terminated name.
 * @param b NUL-terminated name.
 * @return true if they name the same word.
 */
static bool ff_dict_name_eq(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (; ff_dict_fold(*p) == ff_dict_fold(*q); ++p, ++q)
        if (*p == '\0')
            return true;
    return false;
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
 * Unlink @p w from the bucket its current name hashes to.
 *
 * @param d Dictionary.
 * @param w Word to unlink.
 */
static void ff_dict_bucket_unlink(ff_dict_t *d, ff_word_t *w)
{
    size_t i = (size_t)(ff_dict_hash(w->name) & (d->bucket_count - 1));
    ff_word_t **link = &d->buckets[i];
    while (*link && *link != w)
        link = &(*link)->next_bucket;
    if (*link == w)
        *link = w->next_bucket;
}

/**
 * Wipe every chain and reinsert each surviving word in append order
 * so the newest-wins property holds. Called by @ref ff_dict_truncate
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


/**
 * Give the arena back from position @p from, after words were removed:
 * everything allocated after it belonged to them. A remaining word's
 * heap can lie beyond it too — a definition that kept growing after a
 * word was created between its `[` and `]` — so stop short of the last
 * such heap.
 *
 * @param d    Dictionary.
 * @param from Arena position where the first removed word began.
 */
static void ff_dict_reclaim(ff_dict_t *d, ff_arena_mark_t from)
{
    for (size_t i = 0; i < d->count; ++i)
    {
        const ff_heap_t *h = &d->words[i]->heap;
        ff_arena_mark_t end;
        if (h->arena == &d->arena && h->data && h->capacity
                && ff_arena_locate_end(&d->arena, h->data,
                                       h->capacity * sizeof(ff_int_t), &end)
                && ff_arena_after(end, from))
            from = end;
    }
    ff_arena_release(&d->arena, from);
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
    d->arena.mem = &d->mem;
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
        if (ff_dict_name_eq(w->name, name))
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
            if (ff_dict_name_eq(w->name, name))
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

/** @copydoc ff_dict_word_cost */
size_t ff_dict_word_cost(const char *name)
{
    return sizeof(ff_word_t) + sizeof(ff_word_t *) + strlen(name) + 1;
}

/** @copydoc ff_dict_append */
ff_word_t *ff_dict_append(ff_dict_t *d, ff_word_t *w)
{
    if (!w)
        return NULL;
    if (!ff_dict_ensure(d, 1))
    {
        ff_word_free(w);
        return NULL;
    }
    d->words[d->count++] = w;
    ff_mem_charge(&d->mem, ff_dict_word_cost(w->name));
    ff_dict_bucket_insert(d, w);
    /* Wire the heap to bump our mutation_seq on every realloc-that-
       moves-data, then bump for this append itself. */
    w->heap.mutation_seq_p = &d->mutation_seq;
    /* Bind the heap to the dict's arena. A heap that already owns a
       malloc'd buffer stays on malloc: mixing the two on one heap would
       free arena memory in ff_heap_destroy or vice versa. */
    if (w->heap.data == NULL)
    {
        w->heap.arena = &d->arena;
        w->heap.mark  = ff_arena_mark(&d->arena);
    }
    ++d->mutation_seq;
    return w;
}

/** @copydoc ff_dict_index */
size_t ff_dict_index(const ff_dict_t *d, const char *name)
{
    /* User words only. Built-ins live in the shared block — forgetting
       one would mutate state seen by every other engine sharing the
       singleton. */
    for (size_t i = d->count; i-- > 0; )
        if (ff_dict_name_eq(d->words[i]->name, name))
            return i;
    return (size_t)-1;
}

/** @copydoc ff_dict_truncate */
void ff_dict_truncate(ff_dict_t *d, size_t index)
{
    if (index >= d->count)
        return;
    /* Every word from here on goes, so the arena can go back to where
       the earliest of them began. (Usually the first; a heap trimmed
       back at `;` can put a later word's start lower.) */
    ff_arena_mark_t from = d->words[index]->heap.mark;
    for (size_t j = index; j < d->count; ++j)
    {
        if (d->words[j]->heap.arena == &d->arena
                && ff_arena_after(from, d->words[j]->heap.mark))
            from = d->words[j]->heap.mark;
        ff_mem_release(&d->mem, ff_dict_word_cost(d->words[j]->name));
        ff_word_free(d->words[j]);
    }
    d->count = index;
    ff_dict_buckets_rebuild(d);
    ff_dict_reclaim(d, from);
    ++d->mutation_seq;
}

/** @copydoc ff_dict_remove */
bool ff_dict_remove(ff_dict_t *d, ff_word_t *w)
{
    /* The word is almost always the newest; search from that end. */
    for (size_t i = d->count; i-- > 0; )
    {
        if (d->words[i] != w)
            continue;
        memmove(&d->words[i], &d->words[i + 1],
                (d->count - i - 1) * sizeof(d->words[0]));
        d->count--;
        ff_dict_bucket_unlink(d, w);
        ff_arena_mark_t from = w->heap.mark;
        bool in_arena = w->heap.arena == &d->arena;
        ff_mem_release(&d->mem, ff_dict_word_cost(w->name));
        ff_word_free(w);
        if (in_arena)
            ff_dict_reclaim(d, from);
        ++d->mutation_seq;
        return true;
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
        ff_interval_t *grown = (ff_interval_t *)realloc(d->intervals,
                                                        nc * sizeof(ff_interval_t));
        if (!grown)
        {
            /* No index: every dictionary address fails the check, which
               is safe, until memory allows a rebuild. */
            *count = 0;
            return d->intervals;
        }
        d->intervals = grown;
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
        /* Bytecode is read-only to a program, which could otherwise
           forge what the interpreter follows. */
        d->intervals[n].writable = ff_word_holds_data(w);
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
 * @return false, leaving the table as it was, if it couldn't grow.
 */
static bool ff_dict_ensure(ff_dict_t *d, size_t extra)
{
    if (d->count + extra > d->capacity)
    {
        size_t nc = d->capacity ? d->capacity : 128;
        while (nc < d->count + extra)
        {
            size_t doubled = nc * 2;
            if (doubled <= nc)          /* doubling wrapped */
            {
                nc = d->count + extra;
                break;
            }
            nc = doubled;
        }
        if (nc > SIZE_MAX / sizeof(ff_word_t *))
            return false;
        ff_word_t **grown = (ff_word_t **)realloc(d->words,
                                                  nc * sizeof(ff_word_t *));
        if (!grown)
            return false;
        d->words = grown;
        d->capacity = nc;
    }
    return true;
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
        if (def->opcode >= 0 && def->opcode < FF_OP_COUNT
                && !b->by_opcode[def->opcode])
            b->by_opcode[def->opcode] = w;
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
}

/** @copydoc ff_builtins_destroy */
void ff_builtins_destroy(ff_builtins_t *b)
{
    if (!b) return;
    /* Built-ins own no heap memory, and their names point at string
       literals. */
    free(b->static_pool);
    free(b->buckets);
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
