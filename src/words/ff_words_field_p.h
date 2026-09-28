#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- field / introspection word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( name-cstr -- word_ptr )  `find` — look up a word; pushes 0 on miss. */
case FF_OP_FIND:
    _FF_SL(1);
    _FF_CHECK_STR(tos);
    tos = (ff_int_t)(intptr_t)
                ff_dict_lookup(&ff->dict, (const char *)(intptr_t)tos);
    _FF_NEXT();

/** ( word_ptr -- name-cstr )  `>name` — get the name field of a word. */
case FF_OP_TO_NAME:
    _FF_SL(1);
    _FF_CHECK_XT((ff_word_t *)(intptr_t)tos);
#if FF_SAFE_MEM
    /* A name is no memory the checks know (and a built-in's may be
       read-only); hand out a copy in the string arena, which they do. */
    _FF_SYNC();
    {
        const char *name = ((ff_word_t *)(intptr_t)tos)->name;
        char *copy = ff_pad_intern(ff, name, strlen(name));
        if (!copy)
        {
            ff_mem_check(ff);
            goto done;
        }
        tos = (ff_int_t)(intptr_t)copy;
    }
#else
    tos = (ff_int_t)(intptr_t)((ff_word_t *)(intptr_t)tos)->name;
#endif
    _FF_NEXT();

/** ( word_ptr -- a )  `>body` — get pointer to the heap data of a word. */
case FF_OP_TO_BODY:
    _FF_SL(1);
    _FF_CHECK_XT((ff_word_t *)(intptr_t)tos);
    tos = (ff_int_t)(intptr_t)((ff_word_t *)(intptr_t)tos)->heap.data;
    _FF_NEXT();
