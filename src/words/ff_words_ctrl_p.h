/*
 * ff --- control-flow word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( -- )  R: ( -- ret )  Enter a colon-def: push current ip to R. */
case FF_OP_NEST:
    _FF_RSO(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, ff->cur_word);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);
        ff->cur_word = nw;
        ip = nw->heap.data;
    }
    _FF_NEXT();

/** ( -- )  Tail-call NEST: enter a colon-def without saving a return frame. */
case FF_OP_TNEST:
    /* Tail-call NEST: emitted by the SEMICOLON peephole when a colon-def
       ends with `... NEST x EXIT`. The current frame is being abandoned,
       so we don't push to R or BT — when the called word EXITs it pops
       the older return address (our caller's), giving the same observable
       result as `NEST + EXIT` but using one less return-stack slot per
       chained tail call. */
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        ff->cur_word = nw;
        ip = nw->heap.data;
    }
    _FF_NEXT();

/** ( -- )  R: ( ret -- )  Return from a colon-def. */
case FF_OP_EXIT:
    _FF_RSL(1);
    ip = (ff_int_t *)(intptr_t)*ff_tos(R);
    R->top--;
    if (!ip)
        goto done;
    if (ff->state & FF_STATE_BROKEN)
        goto broken;
    _FF_NEXT();

/** ( -- )  Unconditional jump by the inline offset cell. */
case FF_OP_BRANCH:
    ip += *ip;
    _FF_NEXT();

/** ( flag -- )  Pop and jump if zero. */
case FF_OP_QBRANCH:
    _FF_SL(1);
    if (tos == 0)
        ip += *ip;
    else
        ip++;
    _DROP();
    _FF_NEXT();

/** ( limit start -- )  R: ( -- leave-target limit index )  Runtime DO entry. */
case FF_OP_XDO:
    _FF_SL(2);
    _FF_RSO(3);
    ff_stack_push(R, (ff_int_t)(intptr_t)(ip + *ip));
    ip++;
    ff_stack_push(R, _NOS);
    ff_stack_push(R, tos);
    _DROPN(2);
    _FF_NEXT();

/** ( limit start -- )  Runtime ?DO entry: skip body when start == limit. */
case FF_OP_XQDO:
    _FF_SL(2);
    if (tos == _NOS)
    {
        ip += *ip;
        _DROPN(2);
    }
    else
    {
        _FF_RSO(3);
        ff_stack_push(R, (ff_int_t)(intptr_t)(ip + *ip));
        ip++;
        ff_stack_push(R, _NOS);
        ff_stack_push(R, tos);
        _DROPN(2);
    }
    _FF_NEXT();

/** ( -- )  Runtime LOOP back-edge: increment index, branch unless done. */
case FF_OP_XLOOP:
    _FF_RSL(3);
    *ff_tos(R) += 1;
    if (*ff_tos(R) >= *ff_nos(R))
    {
        ff_stack_popn(R, 3);
        ip++;
    }
    else
        ip += *ip;
    _FF_NEXT();

/** ( delta -- )  Runtime +LOOP back-edge with arbitrary index delta. */
case FF_OP_PXLOOP:
    _FF_SL(1);
    _FF_RSL(3);
    {
        ff_int_t niter = *ff_tos(R) + tos;
        _DROP();
        if (niter >= *ff_nos(R)
                && *ff_tos(R) < *ff_nos(R))
        {
            ff_stack_popn(R, 3);
            ip++;
        }
        else
        {
            ip += *ip;
            *ff_tos(R) = niter;
        }
    }
    _FF_NEXT();

/** ( -- index )  `i` — push the innermost loop's current index. */
case FF_OP_LOOP_I:
    _FF_RSL(3);
    _FF_SO(1);
    _PUSH(*ff_tos(R));
    _FF_NEXT();

/** ( n -- n+i )  Superinstruction emitted by the `i +` peephole. */
case FF_OP_I_ADD:
    _FF_RSL(3);
    _FF_SL(1);
    tos += *ff_tos(R);
    _FF_NEXT();

/** ( -- )  `leave` — exit innermost counted loop early. */
case FF_OP_LEAVE:
    _FF_RSL(3);
    ip = (ff_int_t *)(intptr_t)*ff_sat(R, 2);
    ff_stack_popn(R, 3);
    _FF_NEXT();

/** ( n -- n n | 0 -- 0 )  `?dup` — duplicate iff non-zero. */
case FF_OP_QDUP:
    _FF_SL(1);
    if (tos != 0)
    {
        _FF_SO(1);
        _PUSH(tos);
    }
    _FF_NEXT();

/** ( -- index )  `j` — push the next-outer loop's current index. */
case FF_OP_LOOP_J:
    _FF_RSL(6);
    _FF_SO(1);
    _PUSH(*ff_sat(R, 3));
    _FF_NEXT();

/** ( -- )  `quit` — clear return stack and exit ff_exec. */
case FF_OP_QUIT:
    R->top = 0;
    ip = NULL;
    goto done;

/** ( -- )  `abort` — reset engine state and exit ff_exec. */
case FF_OP_ABORT:
    _FF_SYNC();
    ff_abort(ff);
    _FF_RESTORE();
    goto done;

/** ( n -- | i*x n -- )  `throw` — non-zero raises an exception; the
    most recent CATCH absorbs it. Zero is a no-op. */
case FF_OP_THROW:
    _FF_SL(1);
    if (tos == 0)
    {
        _DROP();
    }
    else
    {
        ff->throw_code = tos;
        _DROP();
        ff->state |= FF_STATE_BROKEN | FF_STATE_THROWN;
        _FF_SYNC();
        goto broken;
    }
    _FF_NEXT();

/** ( i*x xt -- j*x 0 | i*x n )  `catch` — execute xt; push 0 on clean
    return, or restore stacks and push the THROW code on exception. */
case FF_OP_CATCH:
    _FF_SL(1);
    {
        ff_word_t *xt = (ff_word_t *)(intptr_t)tos;
        _FF_CHECK_XT(xt);
        _DROP();
        /* Snapshot before the protected call. ff_exec leaves ff->ip
           cleared on return, so we also save the *outer* ip so the
           caller's bytecode position survives the nested run. */
        size_t saved_s  = S->top;
        size_t saved_r  = R->top;
        int    saved_bt = BT->top;
        ff_int_t   *saved_outer_ip = ip;
        ff_word_t  *saved_cur      = ff->cur_word;
        _FF_SYNC();
        ff_exec(ff, xt);
        ff->ip = saved_outer_ip;
        _FF_RESTORE();
        if (ff->state & FF_STATE_THROWN)
        {
            /* Unwind: roll the stacks back, clear the broken/thrown
               flags, and push the throw code. */
            S->top  = saved_s;
            R->top  = saved_r;
            BT->top = saved_bt;
            ff->cur_word = saved_cur;
            ff->state &= ~(FF_STATE_BROKEN | FF_STATE_THROWN);
            if (S->top > 0)
                tos = S->data[S->top - 1];
            _PUSH(ff->throw_code);
        }
        else
        {
            _PUSH(0);
        }
    }
    _FF_NEXT();

/** ( -- bp )  `if` — emit forward QBRANCH placeholder (immediate). */
case FF_OP_IF:
    _FF_COMPILING;
    _FF_SO(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_QBRANCH);
        ff_heap_compile_int(h, 0);
        _PUSH((ff_int_t)(h->size - 1));
    }
    _FF_NEXT();

/** ( bp1 -- bp2 )  `else` — patch IF, emit forward BRANCH placeholder. */
case FF_OP_ELSE:
    _FF_COMPILING;
    _FF_SL(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_BRANCH);
        ff_heap_compile_int(h, 0);
        int bp = (int)tos;
        h->data[bp] = h->size - bp;
        tos = h->size - 1;
    }
    _FF_NEXT();

/** ( bp -- )  `then` — patch the matching forward branch. */
case FF_OP_THEN:
    _FF_COMPILING;
    _FF_SL(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        int bp = (int)tos;
        h->data[bp] = h->size - bp;
        _DROP();
    }
    _FF_NEXT();

/** ( -- target )  `begin` — record the current heap position. */
case FF_OP_BEGIN:
    _FF_COMPILING;
    _FF_SO(1);
    _PUSH((ff_int_t)ff_dict_top(&ff->dict)->heap.size);
    _FF_NEXT();

/** ( target -- )  `until` — emit conditional back-branch. */
case FF_OP_UNTIL:
    _FF_COMPILING;
    _FF_SL(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_QBRANCH);
        ff_heap_compile_int(h, -(ff_int_t)(h->size - tos));
        _DROP();
    }
    _FF_NEXT();

/** ( target -- )  `again` — emit unconditional back-branch. */
case FF_OP_AGAIN:
    _FF_COMPILING;
    _FF_SL(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_BRANCH);
        ff_heap_compile_int(h, -(ff_int_t)(h->size - tos));
        _DROP();
    }
    _FF_NEXT();

/** ( target -- target bp )  `while` — emit forward QBRANCH inside BEGIN..REPEAT. */
case FF_OP_WHILE:
    _FF_COMPILING;
    _FF_SO(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_QBRANCH);
        ff_heap_compile_int(h, 0);
        _PUSH((ff_int_t)(h->size - 1));
    }
    _FF_NEXT();

/** ( target bp -- )  `repeat` — back-branch and patch WHILE's forward branch. */
case FF_OP_REPEAT:
    _FF_COMPILING;
    _FF_SL(2);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        int bp1 = (int)tos;
        _DROP();
        ff_heap_compile_op(h, FF_OP_BRANCH);
        int bp = (int)tos;
        ff_heap_compile_int(h, -(ff_int_t)(h->size - bp));
        h->data[bp1] = h->size - bp1;
        _DROP();
    }
    _FF_NEXT();

/** ( -- bp )  `do` — emit XDO + leave-offset placeholder. */
case FF_OP_DO:
    _FF_COMPILING;
    _FF_SO(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_XDO);
        ff_heap_compile_int(h, 0);
        _PUSH((ff_int_t)h->size);
    }
    _FF_NEXT();

/** ( -- bp )  `?do` — emit XQDO + leave-offset placeholder. */
case FF_OP_QDO:
    _FF_COMPILING;
    _FF_SO(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_XQDO);
        ff_heap_compile_int(h, 0);
        _PUSH((ff_int_t)h->size);
    }
    _FF_NEXT();

/** ( bp -- )  `loop` — emit XLOOP + back-offset, patch leave-target. */
case FF_OP_LOOP:
    _FF_COMPILING;
    _FF_SL(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_XLOOP);
        int bp = (int)tos;
        ff_heap_compile_int(h, -(ff_int_t)(h->size - bp));
        h->data[bp - 1] = h->size - bp + 1;
        _DROP();
    }
    _FF_NEXT();

/** ( bp -- )  `+loop` — emit PXLOOP + back-offset, patch leave-target. */
case FF_OP_PLOOP:
    _FF_COMPILING;
    _FF_SL(1);
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        ff_heap_compile_op(h, FF_OP_PXLOOP);
        int bp = (int)tos;
        ff_heap_compile_int(h, -(ff_int_t)(h->size - bp));
        h->data[bp - 1] = h->size - bp + 1;
        _DROP();
    }
    _FF_NEXT();

/**
 * `abort"` — at compile-time set up the inline string anticipation;
 * at runtime print the inline string and abort the engine.
 */
case FF_OP_ABORTQ:
    /* If invoked at compile time (direct entry from ff_eval), set up to
       compile (abortq) + string. If invoked at runtime in compiled heap,
       print the inline string and abort. */
    if (ff->state & FF_STATE_COMPILING)
    {
        ff->state |= FF_STATE_STRLIT_ANTIC;
        ff_heap_compile_op(&ff_dict_top(&ff->dict)->heap, FF_OP_ABORTQ);
    }
    else
    {
        _FF_SYNC();
        ff_printf(ff, "%s", (const char *)(ip + 1));
        ff_abort(ff);
        _FF_RESTORE();
        goto done;
    }
    _FF_NEXT();
