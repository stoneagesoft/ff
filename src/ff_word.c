/**
 * @file ff_word.c
 * @brief Lifecycle helpers for @ref ff_word_t.
 *
 * Two creation paths share an init-common helper:
 *   - @ref ff_word_new — heap-allocated struct with strdup'd name;
 *     used for user-defined words and externally registered natives.
 *   - @ref ff_word_init_static — placement init into a caller-owned
 *     slot (the dict's static pool) with the name aliasing a string
 *     literal; tags the word FF_WORD_STATIC so ff_word_free leaves
 *     both the struct and the name alone.
 */

#include "ff_word_p.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>


/**
 * Set up everything that doesn't depend on the storage strategy.
 *
 * Initializes the heap, stashes the external native fn pointer (if
 * any) at heap.data[0] and raises FF_WORD_NATIVE, and parses the
 * manual entry into a separate prototype/description split.
 *
 * @param w      Word being initialized; @c name and @c flags must
 *               already be set by the caller.
 * @param code   External native fn pointer, or NULL for opcoded
 *               words.
 * @param opcode Engine opcode driving execution (FF_OP_NONE if none).
 * @param manual Manual entry, or NULL.
 */
static void ff_word_init_common(ff_word_t *w, ff_word_fn code,
                                ff_opcode_t opcode, const char *manual)
{
    w->opcode = opcode;
    w->manual = manual;

    ff_heap_init(&w->heap);

    /* External native words go through the FF_OP_CALL escape hatch. We
       stash the function pointer at heap.data[0] and tag the word with
       FF_WORD_NATIVE so callers (ff_exec direct entry, ff_heap_compile_word)
       can retrieve it without a separate side table. */
    if (code)
    {
        ff_heap_compile_int(&w->heap, (ff_int_t)(intptr_t)code);
        w->flags |= FF_WORD_NATIVE;
    }

    /* Parse manual: find description after first newline. */
    if (manual)
    {
        const char *p = manual;
        while (*p && isspace((unsigned char)*p))
            p++;
        w->man_desc = strchr(p, '\n');
        if (w->man_desc)
            w->man_desc++;
    }
}


// Public

/**
 * @copydoc ff_word_new
 */
ff_word_t *ff_word_new(const char *name, ff_word_fn code,
                       ff_opcode_t opcode, const char *manual)
{
    ff_word_t *w = (ff_word_t *)calloc(1, sizeof(ff_word_t));
    w->name = ff_strdup(name);
    ff_word_init_common(w, code, opcode, manual);
    return w;
}

/**
 * @copydoc ff_word_init_static
 */
void ff_word_init_static(ff_word_t *w, const char *name, ff_word_fn code,
                         ff_opcode_t opcode, const char *manual)
{
    memset(w, 0, sizeof(*w));
    /* `name` aliases a string literal in the registration table — never
       freed individually. The cast away from const matches the (legacy)
       declaration of ff_word_t::name as char *. */
    w->name = (char *)(uintptr_t)name;
    w->flags = FF_WORD_STATIC;
    ff_word_init_common(w, code, opcode, manual);
}

/**
 * @copydoc ff_im_word_new
 */
ff_word_t *ff_im_word_new(const char *name, ff_word_fn code,
                          ff_opcode_t opcode, const char *manual)
{
    ff_word_t *w = ff_word_new(name, code, opcode, manual);
    w->flags |= FF_WORD_IMMEDIATE;

    return w;
}

/**
 * @copydoc ff_word_free
 */
void ff_word_free(ff_word_t *w)
{
    if (!w)
        return;
    /* FF_WORD_STATIC words live in the dict's pre-allocated pool with
       their names pointing into string literals — neither the struct
       nor the name is freed here; the heap may still hold an external
       native's fn pointer (single allocation) which we do release. */
    ff_heap_destroy(&w->heap);
    if (w->flags & FF_WORD_STATIC)
        return;
    free(w->name);
    free(w);
}

/**
 * @copydoc ff_word_is_native
 */
bool ff_word_is_native(const ff_word_t *w)
{
    /* Built-ins have an empty heap; external natives stash their fn
       pointer at heap.data[0] and carry FF_WORD_NATIVE. Colon-defs and
       DOES>/CREATE-runtime words have non-empty heaps without the flag. */
    return w->heap.size == 0 || (w->flags & FF_WORD_NATIVE);
}

/**
 * @copydoc ff_word_native_fn
 */
ff_word_fn ff_word_native_fn(const ff_word_t *w)
{
    return (ff_word_fn)(intptr_t)w->heap.data[0];
}
