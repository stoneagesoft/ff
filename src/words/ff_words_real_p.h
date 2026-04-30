/*
 * ff --- floating-point word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * Real values are stored as `ff_int_t`-sized bit patterns. With the TOS
 * cached in a register, we read/write the floating value directly out of
 * the `tos` lvalue via ff_get_real(&tos) / ff_set_real(&tos, r) instead
 * of going through the ff_real0/ff_set_real0 helpers, which would have
 * gone via ff->stack memory (and so seen a stale slot).
 */

/** ( -- r )  `(flit)` — push the inline real-literal cell. */
case FF_OP_FLIT:
    _FF_SO(1);
    _FF_PUSH(*ip++);
    _FF_NEXT();

/** ( r1 r2 -- r3 )  `f+` — real addition. */
case FF_OP_FADD:
    _FF_SL(2);
    ff_set_real(&tos, ff_get_real(&_FF_NOS) + ff_get_real(&tos));
    --S->top;
    _FF_NEXT();

/** ( r1 r2 -- r3 )  `f-` — real subtraction. */
case FF_OP_FSUB:
    _FF_SL(2);
    ff_set_real(&tos, ff_get_real(&_FF_NOS) - ff_get_real(&tos));
    --S->top;
    _FF_NEXT();

/** ( r1 r2 -- r3 )  `f*` — real multiplication. */
case FF_OP_FMUL:
    _FF_SL(2);
    ff_set_real(&tos, ff_get_real(&_FF_NOS) * ff_get_real(&tos));
    --S->top;
    _FF_NEXT();

/** ( r1 r2 -- r3 )  `f/` — real division; raises FF_ERR_DIV_ZERO if r2 is 0. */
case FF_OP_FDIV:
    _FF_SL(2);
    if (ff_get_real(&tos) == 0.0)
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_DIV_ZERO, "Division by real zero.");
        goto done;
    }
    ff_set_real(&tos, ff_get_real(&_FF_NOS) / ff_get_real(&tos));
    --S->top;
    _FF_NEXT();

/** ( r -- -r )  `fnegate`. */
case FF_OP_FNEGATE:
    _FF_SL(1);
    ff_set_real(&tos, -ff_get_real(&tos));
    _FF_NEXT();

/** ( r -- |r| )  `fabs`. */
case FF_OP_FABS:
    _FF_SL(1);
    ff_set_real(&tos, fabs(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- sqrt(r) )  `fsqrt`. */
case FF_OP_FSQRT:
    _FF_SL(1);
    ff_set_real(&tos, sqrt(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- sin(r) )  `fsin`. */
case FF_OP_FSIN:
    _FF_SL(1);
    ff_set_real(&tos, sin(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- cos(r) )  `fcos`. */
case FF_OP_FCOS:
    _FF_SL(1);
    ff_set_real(&tos, cos(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- tan(r) )  `ftan`. */
case FF_OP_FTAN:
    _FF_SL(1);
    ff_set_real(&tos, tan(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- asin(r) )  `fasin`. */
case FF_OP_FASIN:
    _FF_SL(1);
    ff_set_real(&tos, asin(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- acos(r) )  `facos`. */
case FF_OP_FACOS:
    _FF_SL(1);
    ff_set_real(&tos, acos(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- atan(r) )  `fatan`. */
case FF_OP_FATAN:
    _FF_SL(1);
    ff_set_real(&tos, atan(ff_get_real(&tos)));
    _FF_NEXT();

/** ( y x -- atan2(y,x) )  `fatan2`. */
case FF_OP_FATAN2:
    _FF_SL(2);
    ff_set_real(&tos, atan2(ff_get_real(&_FF_NOS), ff_get_real(&tos)));
    --S->top;
    _FF_NEXT();

/** ( r -- exp(r) )  `fexp`. */
case FF_OP_FEXP:
    _FF_SL(1);
    ff_set_real(&tos, exp(ff_get_real(&tos)));
    _FF_NEXT();

/** ( r -- log(r) )  `flog`. */
case FF_OP_FLOG:
    _FF_SL(1);
    ff_set_real(&tos, log(ff_get_real(&tos)));
    _FF_NEXT();

/** ( base exp -- pow(base,exp) )  `fpow`. */
case FF_OP_FPOW:
    _FF_SL(2);
    ff_set_real(&tos, pow(ff_get_real(&_FF_NOS), ff_get_real(&tos)));
    --S->top;
    _FF_NEXT();

/** ( r -- )  `f.` — print TOS as a real number. */
case FF_OP_F_DOT:
    _FF_SL(1);
    _FF_SYNC();
    ff_printf(ff, "%g", ff_get_real(&tos));
    _FF_DROP();
    _FF_NEXT();

/** ( n -- r )  `float` — convert int to real. */
case FF_OP_FLOAT:
    _FF_SL(1);
    ff_set_real(&tos, (ff_real_t)tos);
    _FF_NEXT();

/** ( r -- n )  `fix` — truncate real to int. */
case FF_OP_FIX:
    _FF_SL(1);
    tos = (ff_int_t)ff_get_real(&tos);
    _FF_NEXT();

/** ( -- pi )  `pi` — push 3.14159… */
case FF_OP_PI:
    _FF_SO(1);
    _FF_PUSH_REAL(3.14159265358979323846);
    _FF_NEXT();

/** ( -- e )  `e` — push 2.71828… */
case FF_OP_E_CONST:
    _FF_SO(1);
    _FF_PUSH_REAL(2.71828182845904523536);
    _FF_NEXT();

/** ( r1 r2 -- flag )  `f=`. */
case FF_OP_FEQ:
    _FF_SL(2);
    {
        ff_int_t t = (ff_get_real(&_FF_NOS) == ff_get_real(&tos)) ? FF_TRUE : FF_FALSE;
        --S->top;
        tos = t;
    }
    _FF_NEXT();

/** ( r1 r2 -- flag )  `f<>`. */
case FF_OP_FNEQ:
    _FF_SL(2);
    {
        ff_int_t t = (ff_get_real(&_FF_NOS) != ff_get_real(&tos)) ? FF_TRUE : FF_FALSE;
        --S->top;
        tos = t;
    }
    _FF_NEXT();

/** ( r1 r2 -- flag )  `f<`. */
case FF_OP_FLT:
    _FF_SL(2);
    {
        ff_int_t t = (ff_get_real(&_FF_NOS) < ff_get_real(&tos)) ? FF_TRUE : FF_FALSE;
        --S->top;
        tos = t;
    }
    _FF_NEXT();

/** ( r1 r2 -- flag )  `f>`. */
case FF_OP_FGT:
    _FF_SL(2);
    {
        ff_int_t t = (ff_get_real(&_FF_NOS) > ff_get_real(&tos)) ? FF_TRUE : FF_FALSE;
        --S->top;
        tos = t;
    }
    _FF_NEXT();

/** ( r1 r2 -- flag )  `f<=`. */
case FF_OP_FLE:
    _FF_SL(2);
    {
        ff_int_t t = (ff_get_real(&_FF_NOS) <= ff_get_real(&tos)) ? FF_TRUE : FF_FALSE;
        --S->top;
        tos = t;
    }
    _FF_NEXT();

/** ( r1 r2 -- flag )  `f>=`. */
case FF_OP_FGE:
    _FF_SL(2);
    {
        ff_int_t t = (ff_get_real(&_FF_NOS) >= ff_get_real(&tos)) ? FF_TRUE : FF_FALSE;
        --S->top;
        tos = t;
    }
    _FF_NEXT();
