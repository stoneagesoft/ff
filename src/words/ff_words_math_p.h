/*
 * ff --- math word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * Binary ops follow the pattern `tos = _NOS op tos; --S->top;`. The
 * memory slot at the post-decrement S->data[S->top - 1] is left holding
 * the old NOS — that's fine, per the cached-TOS invariant the slot at
 * S->top - 1 is treated as scratch until the next sync.
 */

/** ( n1 n2 -- n3 )  `+` — n3 = n1 + n2. */
case FF_OP_ADD:
    _FF_SL(2);
    tos = _NOS + tos;
    --S->top;
    _FF_NEXT();

/** ( a b -- a a+b )  Superinstruction: `over +` — common base+offset
    idiom. Saves one push/pop round-trip. */
case FF_OP_OVER_PLUS:
    _FF_SL(2);
    tos += _NOS;
    _FF_NEXT();

/** ( n -- n+r )  Superinstruction: `r@ +` — index-relative offset
    add inside DO loops without the intermediate push. */
case FF_OP_R_PLUS:
    _FF_RSL_T(1);
    _FF_SL(1);
    tos += *ff_tos(R);
    _FF_NEXT();

/** ( a -- 2a )  Superinstruction: `dup +` — double TOS in place,
    saving the dup push then add-and-drop round-trip. */
case FF_OP_DUP_ADD:
    _FF_SL(1);
    tos += tos;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `-` — n3 = n1 - n2. */
case FF_OP_SUB:
    _FF_SL(2);
    tos = _NOS - tos;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `*` — n3 = n1 * n2. */
case FF_OP_MUL:
    _FF_SL(2);
    tos = _NOS * tos;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `/` — n3 = n1 / n2; raises FF_ERR_DIV_ZERO if n2 is 0. */
case FF_OP_DIV:
    _FF_SL(2);
    if (tos == 0)
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_DIV_ZERO, "Division by zero.");
        goto done;
    }
    tos = _NOS / tos;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `mod` — n3 = n1 % n2; raises FF_ERR_DIV_ZERO if n2 is 0. */
case FF_OP_MOD:
    _FF_SL(2);
    if (tos == 0)
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_DIV_ZERO, "Division by zero.");
        goto done;
    }
    tos = _NOS % tos;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `and` — bitwise AND. */
case FF_OP_AND:
    _FF_SL(2);
    tos = _NOS & tos;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `or` — bitwise OR. */
case FF_OP_OR:
    _FF_SL(2);
    tos = _NOS | tos;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `xor` — bitwise exclusive OR. */
case FF_OP_XOR:
    _FF_SL(2);
    tos = _NOS ^ tos;
    --S->top;
    _FF_NEXT();

/** ( n1 -- n2 )  `not` — bitwise complement. */
case FF_OP_NOT:
    _FF_SL(1);
    tos = ~tos;
    _FF_NEXT();

/** ( n1 n2 -- flag )  `=` — flag is FF_TRUE iff n1 == n2. */
case FF_OP_EQ:
    _FF_SL(2);
    tos = (_NOS == tos) ? FF_TRUE : FF_FALSE;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- flag )  `<>` — flag is FF_TRUE iff n1 != n2. */
case FF_OP_NEQ:
    _FF_SL(2);
    tos = (_NOS != tos) ? FF_TRUE : FF_FALSE;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- flag )  `<` — flag is FF_TRUE iff n1 <  n2. */
case FF_OP_LT:
    _FF_SL(2);
    tos = (_NOS < tos) ? FF_TRUE : FF_FALSE;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- flag )  `>` — flag is FF_TRUE iff n1 >  n2. */
case FF_OP_GT:
    _FF_SL(2);
    tos = (_NOS > tos) ? FF_TRUE : FF_FALSE;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- flag )  `<=` — flag is FF_TRUE iff n1 <= n2. */
case FF_OP_LE:
    _FF_SL(2);
    tos = (_NOS <= tos) ? FF_TRUE : FF_FALSE;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- flag )  `>=` — flag is FF_TRUE iff n1 >= n2. */
case FF_OP_GE:
    _FF_SL(2);
    tos = (_NOS >= tos) ? FF_TRUE : FF_FALSE;
    --S->top;
    _FF_NEXT();

/** ( n1 -- flag )  `0=` — flag is FF_TRUE iff n1 is zero. */
case FF_OP_ZERO_EQ:
    _FF_SL(1);
    tos = (tos == 0) ? FF_TRUE : FF_FALSE;
    _FF_NEXT();

/** ( n1 -- flag )  `0<` — flag is FF_TRUE iff n1 is negative. */
case FF_OP_ZERO_LT:
    _FF_SL(1);
    tos = (tos < 0) ? FF_TRUE : FF_FALSE;
    _FF_NEXT();

/** ( n1 -- n2 )  `1+` — increment TOS. */
case FF_OP_INC:
    _FF_SL(1);
    tos += 1;
    _FF_NEXT();

/** ( n1 -- n2 )  `1-` — decrement TOS. */
case FF_OP_DEC:
    _FF_SL(1);
    tos -= 1;
    _FF_NEXT();

/** ( n1 n2 -- rem quot )  `/mod` — Euclidean div+mod in one shot. */
case FF_OP_DIVMOD:
    _FF_SL(2);
    if (tos == 0)
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_DIV_ZERO, "Division by zero.");
        goto done;
    }
    {
        ff_int_t q = _NOS / tos;
        _NOS %= tos;
        tos = q;
    }
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `min` — n3 = min(n1, n2). */
case FF_OP_MIN:
    _FF_SL(2);
    if (tos > _NOS)
        tos = _NOS;
    --S->top;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `max` — n3 = max(n1, n2). */
case FF_OP_MAX:
    _FF_SL(2);
    if (tos < _NOS)
        tos = _NOS;
    --S->top;
    _FF_NEXT();

/** ( n1 -- n2 )  `negate` — arithmetic negation. */
case FF_OP_NEGATE:
    _FF_SL(1);
    tos = -tos;
    _FF_NEXT();

/** ( n1 -- n2 )  `abs` — n2 = |n1|. */
case FF_OP_ABS:
    _FF_SL(1);
    if (tos < 0)
        tos = -tos;
    _FF_NEXT();

/** ( n1 -- flag )  `0<>` — flag is FF_TRUE iff n1 is non-zero. */
case FF_OP_ZERO_NEQ:
    _FF_SL(1);
    tos = (tos != 0) ? FF_TRUE : FF_FALSE;
    _FF_NEXT();

/** ( n1 -- flag )  `0>` — flag is FF_TRUE iff n1 is positive. */
case FF_OP_ZERO_GT:
    _FF_SL(1);
    tos = (tos > 0) ? FF_TRUE : FF_FALSE;
    _FF_NEXT();

/** ( n1 n2 -- n3 )  `shift` — n2 > 0 left-shifts, n2 < 0 right-shifts. */
case FF_OP_SHIFT:
    _FF_SL(2);
    tos = tos < 0
                ? (_NOS >> (-tos))
                : (_NOS << tos);
    --S->top;
    _FF_NEXT();

/** ( n1 -- n2 )  `2+` — add 2. */
case FF_OP_INC2:
    _FF_SL(1);
    tos += 2;
    _FF_NEXT();

/** ( n1 -- n2 )  `2-` — subtract 2. */
case FF_OP_DEC2:
    _FF_SL(1);
    tos -= 2;
    _FF_NEXT();

/** ( n1 -- n2 )  `2*` — multiply by 2. */
case FF_OP_MUL2:
    _FF_SL(1);
    tos *= 2;
    _FF_NEXT();

/** ( n1 -- n2 )  `2/` — divide by 2. */
case FF_OP_DIV2:
    _FF_SL(1);
    tos /= 2;
    _FF_NEXT();

/** ( n -- )  `base` — set numeric base; only 10 and 16 are accepted. */
case FF_OP_SET_BASE:
    _FF_SL(1);
    switch (tos)
    {
        case 10: ff->base = FF_BASE_DEC; break;
        case 16: ff->base = FF_BASE_HEX; break;
        default:
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_APPLICATION,
                      "Unsupported base %ld.", (long)tos);
            goto done;
    }
    _DROP();
    _FF_NEXT();
