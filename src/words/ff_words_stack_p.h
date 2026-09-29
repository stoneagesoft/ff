#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- stack word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( -- n )  `(lit)` — push the inline-literal cell that follows. */
case FF_OP_LIT:
    _FF_SO(1);
    _FF_PUSH(*ip++);
    _FF_NEXT();

/** ( -- 0 )  Specialized push of the constant 0. */
case FF_OP_LIT0:
    _FF_SO(1);
    _FF_PUSH(0);
    _FF_NEXT();

/** ( -- 1 )  Specialized push of the constant 1. */
case FF_OP_LIT1:
    _FF_SO(1);
    _FF_PUSH(1);
    _FF_NEXT();

/** ( -- -1 )  Specialized push of the constant -1. */
case FF_OP_LITM1:
    _FF_SO(1);
    _FF_PUSH(-1);
    _FF_NEXT();

/** ( a -- a+n )  Superinstruction emitted by the LIT+ADD peephole. */
case FF_OP_LITADD:
    _FF_SL(1);
    /* Superinstruction: TOS += inline literal. */
    tos += *ip++;
    _FF_NEXT();

/** ( a -- a-n )  Superinstruction emitted by the LIT+SUB peephole. */
case FF_OP_LITSUB:
    _FF_SL(1);
    /* Superinstruction: TOS -= inline literal. */
    tos -= *ip++;
    _FF_NEXT();

/** ( -- n )  `depth` — push current stack depth (before pushing). Inside
    a `{ }` scope that is the depth above the barrier: the cells below it
    are not the scope's to count. */
case FF_OP_DEPTH:
    _FF_SO(1);
    _FF_PUSH((ff_int_t)(S->top - floor));
    _FF_NEXT();

/** ( ... -- )  `clear` — discard every data-stack item above the scope
    barrier (all of them outside a scope). Clearing past the barrier would
    destroy the caller's cells and the scope's own inputs. */
case FF_OP_CLEAR:
    S->top = floor;
    _FF_LOAD_TOS();
    _FF_NEXT();

/** ( a b c -- c a b )  `-rot` — reverse three-cell rotate. */
case FF_OP_NROT:
    _FF_SL(3);
    {
        ff_int_t t = tos;
        tos = _FF_NOS;
        _FF_NOS = _FF_SAT(2);
        _FF_SAT(2) = t;
    }
    _FF_NEXT();

/** ( ... idx -- ... item )  `roll` — rotate item at depth idx to TOS. */
case FF_OP_ROLL:
    _FF_SL(1);
    {
        /* Checked as a whole cell, against the items below the index,
           before anything moves: truncated to int, a huge index wrapped
           to a small one or overflowed past the depth check, and a check
           after the pop left the index written over the item below. */
        ff_int_t idx   = tos;
        ff_int_t below = (ff_int_t)(S->top - floor) - 1;
        if (ff_unlikely(idx < 0 || idx >= below))
        {
            _FF_SYNC();
            if (idx < 0)
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                          "Negative roll index.");
            else
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                          "Stack underflow: roll index %" FF_PRIdCELL
                          " reaches past the stack.", idx);
            goto done;
        }
        --S->top;
        /* The selected item becomes the new TOS; items above it shift
           down. We don't load tos from memory here — it's overwritten by
           the rolled value at the end. */
        size_t   k = (size_t)idx;
        ff_int_t t = S->data[S->top - 1 - k];
        for (size_t j = k; j > 0; --j)
            S->data[S->top - 1 - j] = S->data[S->top - j];
        tos = t;
    }
    _FF_NEXT();

/** ( a -- a a )  `dup` — duplicate TOS. */
case FF_OP_DUP:
    _FF_SL(1);
    _FF_SO(1);
    _FF_PUSH(tos);
    _FF_NEXT();

/** ( a -- )  `drop` — discard TOS. */
case FF_OP_DROP:
    _FF_SL(1);
    _FF_DROP();
    _FF_NEXT();

/** ( a b -- b a )  `swap` — exchange top two cells. */
case FF_OP_SWAP:
    _FF_SL(2);
    {
        ff_int_t t = tos;
        tos = _FF_NOS;
        _FF_NOS = t;
    }
    _FF_NEXT();

/** ( a b -- a b a )  `over` — copy NOS to top. */
case FF_OP_OVER:
    _FF_SL(2);
    _FF_SO(1);
    _FF_PUSH(_FF_NOS);
    _FF_NEXT();

/** ( a b -- b )  `nip` — drop NOS. Standalone primitive and the
    target of the `swap drop` peephole. */
case FF_OP_NIP:
    _FF_SL(2);
    _FF_NOS = tos;
    --S->top;
    _FF_NEXT();

/** ( a b -- b a b )  `tuck` — copy TOS under NOS. Standalone
    primitive and the target of the `swap over` peephole.

    Layout invariant: S->data[top-1] is scratch, real TOS is in
    `tos`. Before: top=N, mem[N-2]=NOS. After: top=N+1, mem[N-2]=TOS,
    mem[N-1]=NOS, scratch slot at mem[N], tos register unchanged. */
case FF_OP_TUCK:
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
case FF_OP_ROT:
    _FF_SL(3);
    {
        ff_int_t t = tos;
        tos = _FF_SAT(2);
        _FF_SAT(2) = _FF_NOS;
        _FF_NOS = t;
    }
    _FF_NEXT();

/** ( ... idx -- ... item )  `pick` — replace idx with item at depth idx+1. */
case FF_OP_PICK:
    _FF_SL(1);
    {
        /* Checked as a whole cell (see `roll`). */
        ff_int_t idx   = tos;
        ff_int_t below = (ff_int_t)(S->top - floor) - 1;
        if (ff_unlikely(idx < 0 || idx >= below))
        {
            _FF_SYNC();
            if (idx < 0)
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                          "Negative pick index.");
            else
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                          "Stack underflow: pick index %" FF_PRIdCELL
                          " reaches past the stack.", idx);
            goto done;
        }
        tos = _FF_SAT((size_t)idx + 1);
    }
    _FF_NEXT();

/** ( a -- )  R: ( -- a )  `>r` — pop data stack, push return stack. */
case FF_OP_TO_R:
    _FF_SL(1);
    _FF_RSO(1);
    ff_stack_push(R, tos);
    _FF_DROP();
    _FF_NEXT();

/** ( -- a )  R: ( a -- )  `r>` — pop return stack, push data stack. */
case FF_OP_FROM_R:
    _FF_RSL(1);
    _FF_SO(1);
    _FF_PUSH(*ff_tos(R));
    R->top--;
    _FF_NEXT();

/** ( -- a )  `r@` — copy return-stack TOS to data stack (no R-pop). */
case FF_OP_FETCH_R:
    _FF_RSL(1);
    _FF_SO(1);
    _FF_PUSH(*ff_tos(R));
    _FF_NEXT();
