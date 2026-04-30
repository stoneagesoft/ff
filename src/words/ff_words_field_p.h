/*
 * ff --- field / introspection word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( name-cstr -- word_ptr )  `find` — look up a word; pushes 0 on miss. */
case FF_OP_FIND:
    _FF_SL(1);
    tos = (ff_int_t)(intptr_t)
                ff_dict_lookup(&ff->dict, (const char *)(intptr_t)tos);
    _FF_NEXT();

/** ( word_ptr -- name-cstr )  `>name` — get the name field of a word. */
case FF_OP_TO_NAME:
    _FF_SL(1);
    tos = (ff_int_t)(intptr_t)((ff_word_t *)(intptr_t)tos)->name;
    _FF_NEXT();

/** ( word_ptr -- a )  `>body` — get pointer to the heap data of a word. */
case FF_OP_TO_BODY:
    _FF_SL(1);
    tos = (ff_int_t)(intptr_t)((ff_word_t *)(intptr_t)tos)->heap.data;
    _FF_NEXT();
