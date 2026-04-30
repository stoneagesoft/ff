/*
 * ff --- debug word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( n -- )  `trace` — toggle FF_STATE_TRACE based on n. */
case FF_OP_TRACE:
    _FF_SL(1);
    if (tos)
        ff->state |= FF_STATE_TRACE;
    else
        ff->state &= ~FF_STATE_TRACE;
    _DROP();
    _FF_NEXT();

/** ( n -- )  `backtrace` — toggle FF_STATE_BACKTRACE based on n. */
case FF_OP_BACKTRACE:
    _FF_SL(1);
    if (tos)
        ff->state |= FF_STATE_BACKTRACE;
    else
        ff->state &= ~FF_STATE_BACKTRACE;
    _DROP();
    _FF_NEXT();

/** ( -- errno )  `ERRNO` — push the C library errno value. */
case FF_OP_ERRNO:
    _FF_SO(1);
    _PUSH((ff_int_t)errno);
    _FF_NEXT();

/** ( a n -- )  `dump` — hex+ASCII print of n bytes starting at a. */
case FF_OP_DUMP:
    _FF_SL(2);
    _FF_SYNC();
    ff_dump_bytes(ff, (const char *)(intptr_t)_NOS, (size_t)tos);
    _DROPN(2);
    _FF_NEXT();

#ifdef FF_OS_UNIX
/** ( -- )  `memstat` — print process memory usage table. */
case FF_OP_MEMSTAT:
    _FF_SYNC();
    ff_print_memstat(ff);
    _FF_NEXT();
#endif
