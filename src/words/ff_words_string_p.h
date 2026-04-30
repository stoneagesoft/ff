/*
 * ff --- string word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * `string` remains on the FF_OP_CALL path because it depends on
 * `create`-runtime internals that live in ff_words_var.c.
 */

/** ( -- s )  `(strlit)` — push pointer to the inline string payload. */
case FF_OP_STRLIT:
    _FF_SO(1);
    _PUSH_PTR(ip + 1);
    ip += *ip;
    _FF_NEXT();

/** ( n -- )  `string` — create a named buffer of n bytes. */
case FF_OP_STRING:
    _FF_SL(1);
    ff->state |= FF_STATE_DEF_PENDING;
    ff_dict_append(&ff->dict,
                   ff_word_new(" ", NULL, FF_OP_CREATE_RUNTIME, NULL));
    ff_heap_alloc(&ff_dict_top(&ff->dict)->heap,
                  (int)((tos + 1 + sizeof(ff_int_t)) / sizeof(ff_int_t)));
    _DROP();
    _FF_NEXT();

/** ( src dst -- )  `s!` — strcpy(dst, src). */
case FF_OP_S_STORE:
    _FF_SL(2);
    /* `strcpy` reads the source up to '\0' and writes the same span
       to the destination, so both endpoints need the source length
       worth of bytes (plus one for the terminator). */
    _FF_CHECK_ADDR((const void *)(intptr_t)_NOS, 1);
    {
        size_t _n = strlen((const char *)(intptr_t)_NOS) + 1;
        _FF_CHECK_ADDR((const void *)(intptr_t)_NOS, _n);
        _FF_CHECK_ADDR((const void *)(intptr_t)tos, _n);
        memcpy((char *)(intptr_t)tos, (const char *)(intptr_t)_NOS, _n);
    }
    _DROPN(2);
    _FF_NEXT();

/** ( src dst -- )  `s+` — strcat(dst, src). */
case FF_OP_S_CAT:
    _FF_SL(2);
    _FF_CHECK_ADDR((const void *)(intptr_t)_NOS, 1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, 1);
    {
        size_t _src_n = strlen((const char *)(intptr_t)_NOS);
        size_t _dst_n = strlen((const char *)(intptr_t)tos);
        _FF_CHECK_ADDR((const void *)(intptr_t)_NOS, _src_n + 1);
        _FF_CHECK_ADDR((const void *)((intptr_t)tos + _dst_n), _src_n + 1);
        memcpy((char *)(intptr_t)tos + _dst_n,
               (const char *)(intptr_t)_NOS, _src_n + 1);
    }
    _DROPN(2);
    _FF_NEXT();

/** ( s -- n )  `strlen` — replace string with its length. */
case FF_OP_STRLEN:
    _FF_SL(1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, 1);
    tos = (ff_int_t)strlen((const char *)(intptr_t)tos);
    _FF_NEXT();

/** ( s1 s2 -- n )  `strcmp` — -1 / 0 / +1 lexicographic comparison. */
case FF_OP_STRCMP:
    _FF_SL(2);
    _FF_CHECK_ADDR((const void *)(intptr_t)_NOS, 1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, 1);
    {
        int r = strcmp((const char *)(intptr_t)_NOS,
                       (const char *)(intptr_t)tos);
        tos = r == 0 ? 0 : (r > 0 ? 1 : -1);
    }
    --S->top;
    _FF_NEXT();
