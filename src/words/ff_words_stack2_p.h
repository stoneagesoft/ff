/*
 * ff --- double-stack word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( a b -- a b a b )  `2dup` — duplicate the top two cells. */
_FF_CASE(FF_OP_2DUP)
    _FF_SL(2);
    _FF_SO(2);
    {
        ff_int_t a = _NOS, b = tos;
        _PUSH(a);
        _PUSH(b);
    }
    _FF_NEXT();

/** ( a b -- )  `2drop` — discard two cells. */
_FF_CASE(FF_OP_2DROP)
    _FF_SL(2);
    _DROPN(2);
    _FF_NEXT();

/** ( a b c d -- c d a b )  `2swap` — exchange the two pairs. */
_FF_CASE(FF_OP_2SWAP)
    _FF_SL(4);
    {
        ff_int_t a = tos, b = _NOS;
        tos     = _SAT(2);
        _NOS    = _SAT(3);
        _SAT(2) = a;
        _SAT(3) = b;
    }
    _FF_NEXT();

/** ( a b c d -- a b c d a b )  `2over` — copy second pair to top. */
_FF_CASE(FF_OP_2OVER)
    _FF_SL(4);
    _FF_SO(2);
    {
        ff_int_t a = _SAT(3), b = _SAT(2);
        _PUSH(a);
        _PUSH(b);
    }
    _FF_NEXT();
