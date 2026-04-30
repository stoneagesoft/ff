/*
 * ff --- array word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( n -- )  `array` — define a new word that holds n cells of storage. */
case FF_OP_ARRAY:
    _FF_SL(1);
    ff->state |= FF_STATE_DEF_PENDING;
    ff_dict_append(&ff->dict,
                   ff_word_new(" ", NULL, FF_OP_ARRAY_RUNTIME, NULL));
    ff_heap_alloc(&ff_dict_top(&ff->dict)->heap, (int)tos);
    /* Array size is fixed at definition; trim the doubling slack. */
    ff_heap_trim(&ff_dict_top(&ff->dict)->heap);
    _FF_DROP();
    _FF_NEXT();

/** ( idx -- a )  Runtime entry: TOS = array_base + idx. */
case FF_OP_ARRAY_RUNTIME:
    _FF_SL(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        tos = (ff_int_t)(intptr_t)(nw->heap.data + tos);
    }
    _FF_NEXT();
