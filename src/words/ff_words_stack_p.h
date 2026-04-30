/*
 * ff --- stack word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( -- n )  `(lit)` — push the inline-literal cell that follows. */
_FF_CASE(FF_OP_LIT)
    _FF_SO(1);
    _PUSH(*ip++);
    _FF_NEXT();

/** ( -- 0 )  Specialized push of the constant 0. */
_FF_CASE(FF_OP_LIT0)
    _FF_SO(1);
    _PUSH(0);
    _FF_NEXT();

/** ( -- 1 )  Specialized push of the constant 1. */
_FF_CASE(FF_OP_LIT1)
    _FF_SO(1);
    _PUSH(1);
    _FF_NEXT();

/** ( -- -1 )  Specialized push of the constant -1. */
_FF_CASE(FF_OP_LITM1)
    _FF_SO(1);
    _PUSH(-1);
    _FF_NEXT();

/** ( a -- a+n )  Superinstruction emitted by the LIT+ADD peephole. */
_FF_CASE(FF_OP_LITADD)
    _FF_SL(1);
    /* Superinstruction: TOS += inline literal. */
    tos += *ip++;
    _FF_NEXT();

/** ( a -- a-n )  Superinstruction emitted by the LIT+SUB peephole. */
_FF_CASE(FF_OP_LITSUB)
    _FF_SL(1);
    /* Superinstruction: TOS -= inline literal. */
    tos -= *ip++;
    _FF_NEXT();

/** ( -- n )  `depth` — push current stack depth (before pushing). */
_FF_CASE(FF_OP_DEPTH)
    _FF_SO(1);
    _PUSH((ff_int_t)S->top);
    _FF_NEXT();

/** ( ... -- )  `clear` — discard every data-stack item. */
_FF_CASE(FF_OP_CLEAR)
    S->top = 0;
    _FF_NEXT();

/** ( a b c -- c a b )  `-rot` — reverse three-cell rotate. */
_FF_CASE(FF_OP_NROT)
    _FF_SL(3);
    {
        ff_int_t t = tos;
        tos = _NOS;
        _NOS = _SAT(2);
        _SAT(2) = t;
    }
    _FF_NEXT();

/** ( ... idx -- ... item )  `roll` — rotate item at depth idx to TOS. */
_FF_CASE(FF_OP_ROLL)
    _FF_SL(1);
    {
        int idx = (int)tos;
        --S->top;
        _FF_SL(idx + 1);
        /* The selected item becomes the new TOS; items above it shift
           down. We don't load tos from memory here — it's overwritten by
           the rolled value at the end. */
        ff_int_t t = S->data[S->top - 1 - idx];
        for (int j = idx; j > 0; --j)
            S->data[S->top - 1 - j] = S->data[S->top - j];
        tos = t;
    }
    _FF_NEXT();

/** ( a -- a a )  `dup` — duplicate TOS. */
_FF_CASE(FF_OP_DUP)
    _FF_SL(1);
    _FF_SO(1);
    _PUSH(tos);
    _FF_NEXT();

/** ( a -- )  `drop` — discard TOS. */
_FF_CASE(FF_OP_DROP)
    _FF_SL(1);
    _DROP();
    _FF_NEXT();

/** ( a b -- b a )  `swap` — exchange top two cells. */
_FF_CASE(FF_OP_SWAP)
    _FF_SL(2);
    {
        ff_int_t t = tos;
        tos = _NOS;
        _NOS = t;
    }
    _FF_NEXT();

/** ( a b -- a b a )  `over` — copy NOS to top. */
_FF_CASE(FF_OP_OVER)
    _FF_SL(2);
    _FF_SO(1);
    _PUSH(_NOS);
    _FF_NEXT();

/** ( a b -- b )  `nip` — drop NOS. Standalone primitive and the
    target of the `swap drop` peephole. */
_FF_CASE(FF_OP_NIP)
    _FF_SL(2);
    _NOS = tos;
    --S->top;
    _FF_NEXT();

/** ( a b -- b a b )  `tuck` — copy TOS under NOS. Standalone
    primitive and the target of the `swap over` peephole.

    Layout invariant: S->data[top-1] is scratch, real TOS is in
    `tos`. Before: top=N, mem[N-2]=NOS. After: top=N+1, mem[N-2]=TOS,
    mem[N-1]=NOS, scratch slot at mem[N], tos register unchanged. */
_FF_CASE(FF_OP_TUCK)
    _FF_SL(2);
    _FF_SO(1);
    {
        ff_int_t saved_nos = S->data[S->top - 2];
        S->data[S->top - 2] = tos;
        S->data[S->top - 1] = saved_nos;
        ++S->top;
        /* tos register intentionally unchanged. */
    }
    _FF_NEXT();

/** ( a b c -- b c a )  `rot` — rotate three cells leftward. */
_FF_CASE(FF_OP_ROT)
    _FF_SL(3);
    {
        ff_int_t t = tos;
        tos = _SAT(2);
        _SAT(2) = _NOS;
        _NOS = t;
    }
    _FF_NEXT();

/** ( ... idx -- ... item )  `pick` — replace idx with item at depth idx+1. */
_FF_CASE(FF_OP_PICK)
    _FF_SL(1);
    {
        int idx = (int)tos;
        _FF_SL(idx + 2);
        tos = _SAT(idx + 1);
    }
    _FF_NEXT();

/** ( a -- )  R: ( -- a )  `>r` — pop data stack, push return stack. */
_FF_CASE(FF_OP_TO_R)
    _FF_SL(1);
    _FF_RSO(1);
    ff_stack_push(R, tos);
    _DROP();
    _FF_NEXT();

/** ( -- a )  R: ( a -- )  `r>` — pop return stack, push data stack. */
_FF_CASE(FF_OP_FROM_R)
    _FF_RSL(1);
    _FF_SO(1);
    _PUSH(*ff_tos(R));
    R->top--;
    _FF_NEXT();

/** ( -- a )  `r@` — copy return-stack TOS to data stack (no R-pop). */
_FF_CASE(FF_OP_FETCH_R)
    _FF_RSL(1);
    _FF_SO(1);
    _PUSH(*ff_tos(R));
    _FF_NEXT();
