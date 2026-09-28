#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

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
    _FF_PUSH_PTR(ip + 1);
    ip += *ip;
    _FF_NEXT();

/** ( n -- )  `string` — create a named buffer of n bytes. */
case FF_OP_STRING:
    _FF_SL(1);
    _FF_BAD_SIZE(tos < 0, "string", tos);
    _FF_SYNC();
    {
        ff_word_t *nw = ff_def_new(ff, FF_OP_CREATE_RUNTIME);
        if (!nw)
            goto done;
        /* Room for tos bytes and the NUL, rounded up to cells. */
        ff_heap_alloc(&nw->heap, ((size_t)tos + 1) / sizeof(ff_int_t) + 1);
        _FF_CHECK_MEM();
    }
    _FF_DROP();
    _FF_NEXT();

/** ( src dst -- )  `s!` — strcpy(dst, src). */
case FF_OP_S_STORE:
    _FF_SL(2);
    /* `strcpy` reads the source up to '\0' and writes the same span
       to the destination, so the destination needs the source length
       worth of bytes (plus one for the terminator). */
    _FF_CHECK_STR(_FF_NOS);
    {
        size_t _n = strlen((const char *)(intptr_t)_FF_NOS) + 1;
        _FF_CHECK_WRITE((const void *)(intptr_t)tos, _n);
        memmove((char *)(intptr_t)tos, (const char *)(intptr_t)_FF_NOS, _n);
    }
    _FF_DROPN(2);
    _FF_NEXT();

/** ( src dst -- )  `s+` — strcat(dst, src). */
case FF_OP_S_CAT:
    _FF_SL(2);
    _FF_CHECK_STR(_FF_NOS);
    _FF_CHECK_STR(tos);
    {
        size_t _src_n = strlen((const char *)(intptr_t)_FF_NOS);
        size_t _dst_n = strlen((const char *)(intptr_t)tos);
        _FF_CHECK_WRITE((const void *)((intptr_t)tos + _dst_n), _src_n + 1);
        memmove((char *)(intptr_t)tos + _dst_n,
                (const char *)(intptr_t)_FF_NOS, _src_n + 1);
    }
    _FF_DROPN(2);
    _FF_NEXT();

/** ( s -- n )  `strlen` — replace string with its length. */
case FF_OP_STRLEN:
    _FF_SL(1);
    _FF_CHECK_STR(tos);
    tos = (ff_int_t)strlen((const char *)(intptr_t)tos);
    _FF_NEXT();

/** ( s1 s2 -- n )  `strcmp` — -1 / 0 / +1 lexicographic comparison. */
case FF_OP_STRCMP:
    _FF_SL(2);
    _FF_CHECK_STR(_FF_NOS);
    _FF_CHECK_STR(tos);
    {
        int r = strcmp((const char *)(intptr_t)_FF_NOS,
                       (const char *)(intptr_t)tos);
        tos = r == 0 ? 0 : (r > 0 ? 1 : -1);
    }
    --S->top;
    _FF_NEXT();
