/*
 * ff --- eval word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( s -- ec )  `evaluate` — ff_eval() the string at TOS, push the error code. */
_FF_CASE(FF_OP_EVALUATE)
    _FF_SL(1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, 1);
    {
        const char *src = (const char *)(intptr_t)tos;
        _DROP();
        ff_int_t *prev_ip = ff->ip;
        _FF_SYNC();
        ff->ip = NULL;
        ff_error_t ec = ff_eval(ff, src);
        ff->ip = prev_ip;
        _FF_RESTORE();
        _FF_SO(1);
        _PUSH((ff_int_t)ec);
    }
    _FF_NEXT();

/** ( s -- ec )  `load` — ff_load() the file at TOS, push the error code. */
_FF_CASE(FF_OP_LOAD)
    _FF_SL(1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, 1);
    {
        const char *path = (const char *)(intptr_t)tos;
        _DROP();
        _FF_SYNC();
        ff_error_t ec = ff_load(ff, path);
        _FF_RESTORE();
        _FF_SO(1);
        _PUSH((ff_int_t)ec);
    }
    _FF_NEXT();
