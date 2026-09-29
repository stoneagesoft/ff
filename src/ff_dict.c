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

/* True if the region of `bytes` bytes at `region` is the arena's newest
   allocation: it ends where the head slab's free tail begins. */
static bool ff_arena_is_newest(const ff_arena_t *a, const char *region,
                               size_t bytes)
{
    const ff_arena_slab_t *s = a->head;
    return s && region && region + ff_arena_round(bytes) == &s->data[s->used];
}

/* Shrink @p region from @p old_bytes to @p new_bytes. The freed tail goes
   back to the arena only when @p region is its newest allocation;
   otherwise it is sandwiched between live regions and can't be reclaimed
   without compaction. */
static bool ff_arena_trim(ff_arena_t *a, char *region, size_t old_bytes,
                          size_t new_bytes)
{
    if (new_bytes >= old_bytes || !ff_arena_is_newest(a, region, old_bytes))
        return false;
    a->head->used -= ff_arena_round(old_bytes) - ff_arena_round(new_bytes);
    return true;
}

/* Grow @p region from @p old_bytes to @p new_bytes where it lies, which
   is possible only for the arena's newest allocation: into the head
   slab's free tail when it has room, or, when the region is the slab's
   only one, by reallocating the slab (which may move it). Returns the
   region's address, or NULL if it can't grow in place. */
static char *ff_arena_extend(ff_arena_t *a, char *region, size_t old_bytes,
                             size_t new_bytes)
{
    if (!ff_arena_is_newest(a, region, old_bytes)
            || new_bytes > SIZE_MAX - 7 - sizeof(ff_arena_slab_t))
        return NULL;
    ff_arena_slab_t *s = a->head;
    size_t old_r = ff_arena_round(old_bytes);
    size_t new_r = ff_arena_round(new_bytes);
    if (new_r - old_r <= s->cap - s->used)
    {
        s->used += new_r - old_r;
        return region;
    }
    if (region != s->data)
        return NULL;    /* older regions share the slab */

    /* The slab holds nothing else: it can grow as a whole. The account
       is charged the difference, as for a slab of the new size. */
    size_t more = new_r - s->cap;
    if (a->mem && !ff_mem_fits(a->mem, more))
        return NULL;
    ff_arena_slab_t *g = (ff_arena_slab_t *)realloc(s, sizeof(*s) + new_r);
    if (!g)
        return NULL;
    if (a->mem)
        ff_mem_charge(a->mem, more);
    g->cap  = new_r;
    g->used = new_r;
    a->head = g;
    return g->data;
}


/* ---- Region index: every live heap's [lo, hi), sorted by lo. ---- */

/* Index of the first region whose lo is not below @p lo. */
static size_t ff_region_lower(const ff_arena_t *a, uintptr_t lo)
{
    size_t i = 0, j = a->n_regions;
    while (i < j)
    {
        size_t mid = i + (j - i) / 2;
        if (a->regions[mid].lo < lo)
            i = mid + 1;
        else
            j = mid;
    }
    return i;
}

/* Room for one more region, so that recording one can't fail after the
   heap has already moved. */
static bool ff_region_reserve(ff_arena_t *a)
{
    if (a->n_regions < a->cap_regions)
        return true;
    size_t nc = a->cap_regions ? a->cap_regions * 2 : 64;
    if (nc > SIZE_MAX / sizeof(ff_region_t))
        return false;
    ff_region_t *g = (ff_region_t *)realloc(a->regions,
                                            nc * sizeof(ff_region_t));
    if (!g)
        return false;
    a->regions = g;
    a->cap_regions = nc;
    return true;
}

/* Record the region [lo, hi) of heap @p owner (room reserved). */
static void ff_region_insert(ff_arena_t *a, const char *lo, const char *hi,
                             const ff_heap_t *owner)
{
    size_t i = ff_region_lower(a, (uintptr_t)lo);
    memmove(&a->regions[i + 1], &a->regions[i],
            (a->n_regions - i) * sizeof(ff_region_t));
    a->regions[i].lo    = (uintptr_t)lo;
    a->regions[i].hi    = (uintptr_t)hi;
    a->regions[i].owner = owner;
    ++a->n_regions;
}

/* The region starting at @p lo, or NULL. */
static ff_region_t *ff_region_find(ff_arena_t *a, const char *lo)
{
    size_t i = ff_region_lower(a, (uintptr_t)lo);
    return i < a->n_regions && a->regions[i].lo == (uintptr_t)lo
               ? &a->regions[i] : NULL;
}

/* Forget the region starting at @p lo, if there is one. */
static void ff_region_remove(ff_arena_t *a, const char *lo)
{
    ff_region_t *r = ff_region_find(a, lo);
    if (!r)
        return;
    size_t i = (size_t)(r - a->regions);
    memmove(r, r + 1, (a->n_regions - i - 1) * sizeof(ff_region_t));
    --a->n_regions;
}

/** @copydoc ff_arena_heap_grow */
bool ff_arena_heap_grow(ff_arena_t *a, ff_heap_t *h, size_t want, size_t need)
{
    if (!ff_region_reserve(a))
    {
        if (a->mem)
            ff_mem_refuse(a->mem, sizeof(ff_region_t), true);
        return false;
    }

    char  *old       = (char *)h->data;
    size_t old_bytes = h->capacity * sizeof(ff_int_t);
    size_t cells     = want;
    char  *nd        = ff_arena_extend(a, old, old_bytes,
                                       want * sizeof(ff_int_t));
    if (!nd && need < want)
    {
        cells = need;
        nd = ff_arena_extend(a, old, old_bytes, need * sizeof(ff_int_t));
    }
    if (!nd)
    {
        /* A fresh region, the doubled size if the limit leaves room for
           it: a request that fits shouldn't fail for the slack after it. */
        cells = ff_arena_fits(a, want * sizeof(ff_int_t)) ? want : need;
        nd = (char *)ff_arena_alloc(a, cells * sizeof(ff_int_t));
        if (!nd)
            return false;
        if (h->size)
            memcpy(nd, old, h->size * sizeof(ff_int_t));
    }

    if (old)
        ff_region_remove(a, old);
    ff_region_insert(a, nd, nd + cells * sizeof(ff_int_t), h);
    h->data     = (ff_int_t *)nd;
    h->capacity = cells;
    h->end      = ff_arena_mark(a);
    return true;
}

/** @copydoc ff_arena_heap_trim */
void ff_arena_heap_trim(ff_arena_t *a, ff_heap_t *h)
{
    char  *lo        = (char *)h->data;
    size_t old_bytes = h->capacity * sizeof(ff_int_t);
    size_t new_bytes = h->size * sizeof(ff_int_t);
    if (!lo || new_bytes >= old_bytes)
        return;

    bool newest = ff_arena_trim(a, lo, old_bytes, new_bytes);
    if (new_bytes == 0)
    {
        /* An empty region would start where the next allocation does:
           the heap lets go of it. */
        ff_region_remove(a, lo);
        h->data = NULL;
    }
    else
    {
        ff_region_t *r = ff_region_find(a, lo);
        if (r)
            r->hi = (uintptr_t)(lo + new_bytes);
    }
    h->capacity = h->size;
    if (newest)
        h->end = ff_arena_mark(a);
}

/** @copydoc ff_arena_heap_drop */
void ff_arena_heap_drop(ff_arena_t *a, const ff_heap_t *h)
{
    if (h->data)
        ff_region_remove(a, (const char *)h->data);
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
    free(a->regions);
    a->regions     = NULL;
    a->n_regions   = 0;
    a->cap_regions = 0;
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
 * Give the arena back after words were removed: past the end of the last
 * heap still in use, everything belonged to removed words, or to heaps
 * that have since moved. O(words) — each heap records where it ends.
 *
 * @param d Dictionary.
 */
static void ff_dict_reclaim(ff_dict_t *d)
{
    ff_arena_mark_t keep = { 0, 0 };
    for (size_t i = 0; i < d->count; ++i)
    {
        const ff_heap_t *h = &d->words[i]->heap;
        if (h->arena == &d->arena && h->data && ff_arena_after(h->end, keep))
            keep = h->end;
    }
    ff_arena_release(&d->arena, keep);
}

/**
 * Double the user-word hash table once it holds more words than buckets,
 * so chains stay short however many words are defined. Left as it is if
 * memory is short: lookups only get slower.
 *
 * @param d Dictionary.
 */
static void ff_dict_buckets_grow(ff_dict_t *d)
{
    if (d->count <= d->bucket_count || d->bucket_count > SIZE_MAX / 2)
        return;
    size_t nc = d->bucket_count * 2;
    ff_word_t **b = (ff_word_t **)calloc(nc, sizeof(ff_word_t *));
    if (!b)
        return;
    free(d->buckets);
    d->buckets = b;
    d->bucket_count = nc;
    ff_dict_buckets_rebuild(d);
}

/**
 * Where @p w sits, or would sit, in @ref ff_dict::by_addr among its
 * first @p n entries.
 *
 * @param d Dictionary.
 * @param w Any pointer.
 * @param n Entries in use.
 * @return Index of the first entry not below @p w.
 */
static size_t ff_dict_addr_pos(const ff_dict_t *d, const ff_word_t *w,
                               size_t n)
{
    uintptr_t k = (uintptr_t)w;
    size_t i = 0, j = n;
    while (i < j)
    {
        size_t mid = i + (j - i) / 2;
        if ((uintptr_t)d->by_addr[mid] < k)
            i = mid + 1;
        else
            j = mid;
    }
    return i;
}

/**
 * Take @p w out of @ref ff_dict::by_addr, which has @p n entries.
 *
 * @param d Dictionary.
 * @param w Word to take out.
 * @param n Entries in use.
 */
static void ff_dict_addr_remove(ff_dict_t *d, const ff_word_t *w, size_t n)
{
    size_t i = ff_dict_addr_pos(d, w, n);
    if (i < n && d->by_addr[i] == w)
        memmove(&d->by_addr[i], &d->by_addr[i + 1],
                (n - i - 1) * sizeof(d->by_addr[0]));
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
    /* Everything goes: drop the region index first rather than take the
       words' regions out of it one by one as they are freed. */
    d->arena.n_regions = 0;
    for (size_t i = 0; i < d->count; ++i)
        ff_word_free(d->words[i]);
    ff_arena_destroy(&d->arena);
    free(d->words);
    free(d->by_addr);
    free(d->buckets);
    free(d->builtins_used);
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
   "used" bitmap and to detect "is this a built-in?". The address is
   compared as an integer and must fall on a word boundary: under
   FF_SAFE_MEM it can be any cell a program passed as an xt, and a
   pointer into the middle of a built-in used to pass for one. */
static size_t ff_dict_builtin_index(const ff_dict_t *d, const ff_word_t *w)
{
    if (!d->builtins || !w)
        return (size_t)-1;
    uintptr_t base = (uintptr_t)d->builtins->static_pool;
    uintptr_t addr = (uintptr_t)w;
    if (addr < base)
        return (size_t)-1;
    uintptr_t off = addr - base;
    if (off % sizeof(ff_word_t) != 0
            || off / sizeof(ff_word_t) >= d->builtins->static_pool_size)
        return (size_t)-1;
    return (size_t)(off / sizeof(ff_word_t));
}

/** @copydoc ff_dict_is_builtin */
bool ff_dict_is_builtin(const ff_dict_t *d, const ff_word_t *w)
{
    return ff_dict_builtin_index(d, w) != (size_t)-1;
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
    /* The word, its name, its slots in ff_dict::words and
       ff_dict::by_addr, and its heap's entry in the region index. */
    return sizeof(ff_word_t) + 2 * sizeof(ff_word_t *) + sizeof(ff_region_t)
         + strlen(name) + 1;
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
    size_t at = ff_dict_addr_pos(d, w, d->count);
    memmove(&d->by_addr[at + 1], &d->by_addr[at],
            (d->count - at) * sizeof(d->by_addr[0]));
    d->by_addr[at] = w;
    d->words[d->count++] = w;
    ff_mem_charge(&d->mem, ff_dict_word_cost(w->name));
    ff_dict_bucket_insert(d, w);
    ff_dict_buckets_grow(d);
    /* Bind the heap to the dict's arena. A heap that already owns a
       malloc'd buffer stays on malloc: mixing the two on one heap would
       free arena memory in ff_heap_destroy or vice versa. */
    if (w->heap.data == NULL)
        w->heap.arena = &d->arena;
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
    /* Newest first: those are usually last in by_addr and the region
       index too, so taking them out moves little. */
    while (d->count > index)
    {
        ff_word_t *w = d->words[d->count - 1];
        ff_dict_addr_remove(d, w, d->count);
        --d->count;
        ff_mem_release(&d->mem, ff_dict_word_cost(w->name));
        ff_word_free(w);
    }
    ff_dict_buckets_rebuild(d);
    ff_dict_reclaim(d);
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
        ff_dict_addr_remove(d, w, d->count);
        d->count--;
        ff_dict_bucket_unlink(d, w);
        ff_mem_release(&d->mem, ff_dict_word_cost(w->name));
        ff_word_free(w);
        ff_dict_reclaim(d);
        return true;
    }
    return false;
}

/** @copydoc ff_dict_region_at */
const ff_region_t *ff_dict_region_at(const ff_dict_t *d, const void *addr)
{
    const ff_arena_t *a = &d->arena;
    uintptr_t p = (uintptr_t)addr;
    /* The last region starting at or below p is the only candidate. */
    size_t i = 0, j = a->n_regions;
    while (i < j)
    {
        size_t mid = i + (j - i) / 2;
        if (a->regions[mid].lo <= p)
            i = mid + 1;
        else
            j = mid;
    }
    return i > 0 && p < a->regions[i - 1].hi ? &a->regions[i - 1] : NULL;
}

/** @copydoc ff_dict_contains */
bool ff_dict_contains(const ff_dict_t *d, const ff_word_t *w)
{
    size_t i = ff_dict_addr_pos(d, w, d->count);
    return i < d->count && d->by_addr[i] == w;
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
        grown = (ff_word_t **)realloc(d->by_addr, nc * sizeof(ff_word_t *));
        if (!grown)
            return false;   /* words is merely roomier than it needs */
        d->by_addr = grown;
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
