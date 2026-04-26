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
void ff_dict_init(ff_dict_t *d)
{
    memset(d, 0, sizeof(*d));
    d->bucket_count = FF_DICT_INITIAL_BUCKETS;
    d->buckets = (ff_word_t **)calloc(d->bucket_count, sizeof(ff_word_t *));

    /* Allocate the static pool sized to the exact built-in count, then
       hand it to the define path which fills it slot-by-slot. */
    d->static_pool_size = ff_dict_builtin_count();
    d->static_pool = (ff_word_t *)calloc(d->static_pool_size, sizeof(ff_word_t));

    ff_dict_define_words(d);
}

/** @copydoc ff_dict_destroy */
void ff_dict_destroy(ff_dict_t *d)
{
    for (size_t i = 0; i < d->count; ++i)
        ff_word_free(d->words[i]);
    free(d->words);
    free(d->buckets);
    free(d->static_pool);
    memset(d, 0, sizeof(*d));
}

/** @copydoc ff_dict_top */
ff_word_t *ff_dict_top(ff_dict_t *d)
{
    return d->count
                ? d->words[d->count - 1]
                : NULL;
}

/** @copydoc ff_dict_lookup */
ff_word_t *ff_dict_lookup(ff_dict_t *d, const char *name)
{
    size_t i = (size_t)(ff_dict_hash(name) & (d->bucket_count - 1));
    for (ff_word_t *w = d->buckets[i]; w; w = w->next_bucket)
    {
        if (utf8casecmp(w->name, name) == 0)
        {
            w->flags |= FF_WORD_USED;
            return w;
        }
    }
    return NULL;
}

/** @copydoc ff_dict_append */
ff_word_t *ff_dict_append(ff_dict_t *d, ff_word_t *w)
{
    assert(w);

    ff_dict_ensure(d, 1);
    d->words[d->count++] = w;
    ff_dict_bucket_insert(d, w);
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
    for (int i = (int)d->count - 1; i >= 0; --i)
    {
        if (utf8casecmp(d->words[i]->name, name) == 0)
        {
            for (size_t j = i; j < d->count; ++j)
                ff_word_free(d->words[j]);
            d->count = (size_t)i;
            ff_dict_buckets_rebuild(d);
            return true;
        }
    }
    return false;
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

/**
 * Built-in init: place each entry in the dict's static pool rather
 * than mallocing per word. Caller advances `*pool_idx` so successive
 * calls chain across multiple def tables.
 *
 * @param d        Dictionary.
 * @param defs     Sentinel-terminated registration table.
 * @param pool_idx In/out cursor into @ref ff_dict::static_pool.
 */
static void ff_dict_define_static(ff_dict_t *d, const ff_word_def_t *defs,
                                  size_t *pool_idx)
{
    for (const ff_word_def_t *def = defs; def->name; ++def)
    {
        assert(*pool_idx < d->static_pool_size);
        ff_word_t *w = &d->static_pool[(*pool_idx)++];
        ff_word_init_static(w, def->name, def->code, def->opcode, def->manual);
        if (def->is_immediate)
            w->flags |= FF_WORD_IMMEDIATE;
        ff_dict_append(d, w);
    }
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

/**
 * Register every built-in word into the dict's static pool. Called
 * once at the end of @ref ff_dict_init.
 *
 * @param d Dictionary.
 */
void ff_dict_define_words(ff_dict_t *d)
{
    /* Pool-based registration: every built-in fits into d->static_pool
       (sized to the exact total in ff_dict_init). */
    size_t pool_idx = 0;
    ff_dict_define_static(d, FF_ARRAY_WORDS,  &pool_idx);
    ff_dict_define_static(d, FF_COMP_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_CONIO_WORDS,  &pool_idx);
    ff_dict_define_static(d, FF_CTRL_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_DEBUG_WORDS,  &pool_idx);
    ff_dict_define_static(d, FF_DICT_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_EVAL_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_FIELD_WORDS,  &pool_idx);
    ff_dict_define_static(d, FF_FILE_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_HEAP_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_MATH_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_REAL_WORDS,   &pool_idx);
    ff_dict_define_static(d, FF_STACK2_WORDS, &pool_idx);
    ff_dict_define_static(d, FF_STACK_WORDS,  &pool_idx);
    ff_dict_define_static(d, FF_STRING_WORDS, &pool_idx);
    ff_dict_define_static(d, FF_VAR_WORDS,    &pool_idx);
    assert(pool_idx == d->static_pool_size);
}
