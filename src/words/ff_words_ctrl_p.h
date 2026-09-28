#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- control-flow word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( -- )  R: ( -- ret cur )  Enter a colon-def: push the caller's ip
    and cur_word, then jump to the callee's heap. EXIT pops both. */
case FF_OP_NEST:
    _FF_WATCHDOG_TICK();
    _FF_RSO(2);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, ff->cur_word);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);
        ff_stack_push(R, (ff_int_t)(intptr_t)ff->cur_word);
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
    _FF_WATCHDOG_TICK();
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        ff->cur_word = nw;
        ip = nw->heap.data;
    }
    _FF_NEXT();

/** ( -- )  R: ( ret cur -- )  Return from a colon-def. */
case FF_OP_EXIT:
    _FF_RSL_T(2);
    ff->cur_word = (ff_word_t *)(intptr_t)*ff_tos(R);
    R->top--;
    ip = (ff_int_t *)(intptr_t)*ff_tos(R);
    R->top--;
    if (!ip)
        goto done;
    _FF_NEXT();

/** ( -- )  Unconditional jump by the inline offset cell. */
case FF_OP_BRANCH:
    /* Watchdog only on the back-branch (loop AGAIN/REPEAT path). The
       forward case is structural (ELSE) and can't loop. */
    if (*ip < 0)
        _FF_WATCHDOG_TICK();
    ip += *ip;
    _FF_NEXT();

/** ( flag -- )  Pop and jump if zero. */
case FF_OP_QBRANCH:
    _FF_SL(1);
    if (tos == 0)
    {
        if (*ip < 0)
            _FF_WATCHDOG_TICK();
        ip += *ip;
    }
    else
        ip++;
    _FF_DROP();
    _FF_NEXT();

/** ( limit start -- )  R: ( -- leave-target limit index )  Runtime DO entry. */
case FF_OP_XDO:
    _FF_SL(2);
    _FF_RSO(3);
    ff_stack_push(R, (ff_int_t)(intptr_t)(ip + *ip));
    ip++;
    ff_stack_push(R, _FF_NOS);
    ff_stack_push(R, tos);
    _FF_DROPN(2);
    _FF_NEXT();

/** ( limit start -- )  Runtime ?DO entry: skip body when start == limit. */
case FF_OP_XQDO:
    _FF_SL(2);
    if (tos == _FF_NOS)
    {
        ip += *ip;
        _FF_DROPN(2);
    }
    else
    {
        _FF_RSO(3);
        ff_stack_push(R, (ff_int_t)(intptr_t)(ip + *ip));
        ip++;
        ff_stack_push(R, _FF_NOS);
        ff_stack_push(R, tos);
        _FF_DROPN(2);
    }
    _FF_NEXT();

/** ( -- )  Runtime LOOP back-edge: increment index, branch unless done. */
case FF_OP_XLOOP:
    _FF_RSL_T(3);
    *ff_tos(R) += 1;
    if (*ff_tos(R) >= *ff_nos(R))
    {
        ff_stack_popn(R, 3);
        ip++;
    }
    else
    {
        _FF_WATCHDOG_TICK();
        ip += *ip;
    }
    _FF_NEXT();

/** ( delta -- )  Runtime +LOOP back-edge: add delta to the index and loop
    again unless that step crossed the boundary between limit-1 and limit
    (ANS 6.1.0140). Either direction counts, so a negative delta counts
    down to and including the limit. */
case FF_OP_PXLOOP:
    _FF_SL(1);
    _FF_RSL_T(3);
    {
        /* In unsigned arithmetic, so a step wraps instead of overflowing.
           The boundary was crossed iff index - limit changed sign while
           moving toward it; a sign change while moving away is only the
           wrap-around at the far end of the number range. */
        ff_uint_t delta = (ff_uint_t)tos;
        ff_uint_t diff  = (ff_uint_t)*ff_tos(R) - (ff_uint_t)*ff_nos(R);
        ff_uint_t next  = diff + delta;
        _FF_DROP();
        if ((ff_int_t)(diff ^ next) < 0 && (ff_int_t)(diff ^ delta) < 0)
        {
            ff_stack_popn(R, 3);
            ip++;
        }
        else
        {
            _FF_WATCHDOG_TICK();
            ip += *ip;
            *ff_tos(R) = (ff_int_t)((ff_uint_t)*ff_tos(R) + delta);
        }
    }
    _FF_NEXT();

/** ( -- index )  `i` — push the innermost loop's current index. */
case FF_OP_LOOP_I:
    _FF_RSL_T(3);
    _FF_SO(1);
    _FF_PUSH(*ff_tos(R));
    _FF_NEXT();

/** ( n -- n+i )  Superinstruction emitted by the `i +` peephole. */
case FF_OP_I_ADD:
    _FF_RSL_T(3);
    _FF_SL(1);
    tos += *ff_tos(R);
    _FF_NEXT();

/** ( n -- n+i )  Superinstruction: `i + loop` — fused index-add and
    LOOP back-edge. Halves the dispatch count of the canonical
    summing loop `0 N 0 do  i +  loop`. */
case FF_OP_I_ADD_LOOP:
    _FF_RSL_T(3);
    _FF_SL(1);
    tos += *ff_tos(R);          /* i + */
    *ff_tos(R) += 1;            /* index++ */
    if (*ff_tos(R) >= *ff_nos(R))
    {
        ff_stack_popn(R, 3);
        ip++;                   /* skip back-branch offset */
    }
    else
    {
        _FF_WATCHDOG_TICK();
        ip += *ip;              /* loop back */
    }
    _FF_NEXT();

/** ( -- )  `leave` — exit innermost counted loop early. */
case FF_OP_LEAVE:
    _FF_RSL_T(3);
    ip = (ff_int_t *)(intptr_t)*ff_sat(R, 2);
    ff_stack_popn(R, 3);
    _FF_NEXT();

/** ( -- )  R: ( leave-target limit index -- )  Drop the innermost loop's
    parameters. Compiled ahead of an `exit` from inside a loop. */
case FF_OP_UNLOOP:
    _FF_RSL_T(3);
    ff_stack_popn(R, 3);
    _FF_NEXT();

/** ( n -- n n | 0 -- 0 )  `?dup` — duplicate iff non-zero. */
case FF_OP_QDUP:
    _FF_SL(1);
    if (tos != 0)
    {
        _FF_SO(1);
        _FF_PUSH(tos);
    }
    _FF_NEXT();

/** ( -- index )  `j` — push the next-outer loop's current index. */
case FF_OP_LOOP_J:
    _FF_RSL_T(6);
    _FF_SO(1);
    _FF_PUSH(*ff_sat(R, 3));
    _FF_NEXT();

/** ( -- )  `quit` — stop running and return to the host: an exception
    (-56) that no `catch` stops. The outermost evaluation discards the rest
    of its input and returns FF_OK; the data stack is left as it is. */
case FF_OP_QUIT:
    _FF_SYNC();
    ff_throw(ff, FF_THROW_QUIT);
    goto done;

/** ( -- )  `abort` — THROW -1. If nothing catches it, the outermost
    evaluation resets the engine and returns FF_ERR_ABORTED. */
case FF_OP_ABORT:
    _FF_SYNC();
    ff_raise(ff, FF_THROW_ABORT, FF_SEV_ERROR | FF_ERR_ABORTED, "Aborted.");
    goto done;

/** ( n -- | i*x n -- )  `throw` — non-zero raises an exception; the
    most recent CATCH absorbs it. Zero is a no-op. */
case FF_OP_THROW:
    _FF_SL(1);
    if (tos == 0)
    {
        _FF_DROP();
        _FF_NEXT();
    }
    {
        ff_int_t code = tos;
        _FF_DROP();
        _FF_SYNC();
        ff_throw(ff, code);
    }
    goto done;

/** ( i*x xt -- j*x 0 | i*x n )  `catch` — execute xt; push 0 on clean
    return, or restore stacks and push the THROW code on exception. Engine
    errors arrive as their ANS codes (-4 stack underflow, …). The host's
    abort and `quit` go past every catch. */
case FF_OP_CATCH:
    _FF_SL(1);
    {
        ff_word_t *xt = (ff_word_t *)(intptr_t)tos;
        _FF_CHECK_XT(xt);
        _FF_DROP();
        /* Snapshot before the protected call. ff_exec leaves ff->ip
           cleared on return, so we also save the *outer* ip so the
           caller's bytecode position survives the nested run. */
        size_t saved_s  = S->top;
        size_t saved_r  = R->top;
        int    saved_bt = BT->top;
        /* The barrier is part of the state a CATCH rolls back: a THROW
           out of an open scope must not leave the floor pointing into
           cells that no longer exist, or every later check is wrong.
           Snapshot the register, not S->floor — the memory copy is only
           refreshed by the _FF_SYNC() below. */
        size_t saved_floor  = floor;
        size_t saved_scopes = ff->n_scopes;
        ff_int_t   *saved_outer_ip = ip;
        ff_word_t  *saved_cur      = ff->cur_word;
        _FF_SYNC();
        bool ran = ff_exec(ff, xt);
        ff->ip = saved_outer_ip;
        _FF_RESTORE();
        if (!ran)
        {
            if (ff_throw_is_fatal(ff->throw_code))
                goto done;
            /* Unwind: roll the stacks back, settle the exception, and
               push its code. */
            S->top  = saved_s;
            R->top  = saved_r;
            BT->top = saved_bt;
            floor        = saved_floor;   /* register cache … */
            S->floor     = saved_floor;   /* … and its memory copy */
            ff->n_scopes = saved_scopes;
            ff->cur_word = saved_cur;
            ff->state &= ~FF_STATE_THROWN;
            if (S->top > 0)
                tos = S->data[S->top - 1];
            _FF_PUSH(ff->throw_code);
        }
        else
        {
            /* The xt may have filled the stack; the rolled-back branch
               above always has room, since it rewinds past the xt. */
            _FF_SO(1);
            _FF_PUSH(0);
        }
    }
    _FF_NEXT();

/* The compiling words below keep their structures on ff->cf (see
   ff_cf_p.h), not on the data stack: each closer is checked against what
   it closes. They call helpers that may raise, hence the _FF_SYNC()s. */

/** ( -- )  `if` — emit forward QBRANCH placeholder (immediate). */
case FF_OP_IF:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_QBRANCH);
        ff_heap_compile_int(h, 0);
        _FF_CHECK_MEM();
        if (!ff_cf_push(ff, FF_CF_ORIG, "if", h->size - 1))
            goto done;
    }
    _FF_NEXT();

/** ( -- )  `else` — patch IF, emit forward BRANCH placeholder. */
case FF_OP_ELSE:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_top(ff, FF_CF_ORIG, "else", "if");
        if (!c)
            goto done;
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_BRANCH);
        ff_heap_compile_int(h, 0);
        _FF_CHECK_MEM();
        h->data[c->pos] = h->size - c->pos;
        c->opener = "else";
        c->pos    = h->size - 1;
        /* The patched IF target lands here; the else-clause's first
           op must not fuse backward into the BRANCH or earlier. */
        ff_heap_inhibit_peephole(h);
    }
    _FF_NEXT();

/** ( -- )  `then` — patch the matching forward branch. */
case FF_OP_THEN:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_pop(ff, FF_CF_ORIG, "then", "if");
        if (!c)
            goto done;
        ff_heap_t *h = &ff->compiling->heap;
        h->data[c->pos] = h->size - c->pos;
        /* Position after THEN is a forward-branch target; the next
           op must not fold with whatever was the last op of the
           IF/ELSE clause. */
        ff_heap_inhibit_peephole(h);
    }
    _FF_NEXT();

/** ( -- )  `begin` — record the current heap position. */
case FF_OP_BEGIN:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_heap_t *h = &ff->compiling->heap;
        if (!ff_cf_push(ff, FF_CF_DEST, "begin", h->size))
            goto done;
        /* Position is the back-branch target; the next op mustn't
           fold with the previous one. */
        ff_heap_inhibit_peephole(h);
    }
    _FF_NEXT();

/** ( -- )  `until` — emit conditional back-branch. */
case FF_OP_UNTIL:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_pop(ff, FF_CF_DEST, "until", "begin");
        if (!c)
            goto done;
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_QBRANCH);
        ff_heap_compile_int(h, -(ff_int_t)(h->size - c->pos));
        _FF_CHECK_MEM();
    }
    _FF_NEXT();

/** ( -- )  `again` — emit unconditional back-branch. */
case FF_OP_AGAIN:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_pop(ff, FF_CF_DEST, "again", "begin");
        if (!c)
            goto done;
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_BRANCH);
        ff_heap_compile_int(h, -(ff_int_t)(h->size - c->pos));
        _FF_CHECK_MEM();
    }
    _FF_NEXT();

/** ( -- )  `while` — emit forward QBRANCH inside BEGIN..REPEAT. */
case FF_OP_WHILE:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_top(ff, FF_CF_DEST, "while", "begin");
        if (!c)
            goto done;
        ff_cf_t dest = *c;
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_QBRANCH);
        ff_heap_compile_int(h, 0);
        _FF_CHECK_MEM();
        if (!ff_cf_push(ff, FF_CF_ORIG, "while", h->size - 1))
            goto done;
        /* The orig goes *under* the dest, as in ANS: `repeat` finds the
           dest on top, and each further `while` stacks another orig
           beneath it for a `then` after the loop to resolve. */
        ff->cf[ff->n_cf - 2] = ff->cf[ff->n_cf - 1];
        ff->cf[ff->n_cf - 1] = dest;
    }
    _FF_NEXT();

/** ( -- )  `repeat` — back-branch and patch WHILE's forward branch. */
case FF_OP_REPEAT:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_pop(ff, FF_CF_DEST, "repeat", "begin");
        if (!c)
            goto done;
        size_t target = c->pos;
        c = ff_cf_pop(ff, FF_CF_ORIG, "repeat", "while");
        if (!c)
            goto done;
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_BRANCH);
        ff_heap_compile_int(h, -(ff_int_t)(h->size - target));
        _FF_CHECK_MEM();
        h->data[c->pos] = h->size - c->pos;
        /* Position after REPEAT is the WHILE forward target. */
        ff_heap_inhibit_peephole(h);
    }
    _FF_NEXT();

/** ( -- )  `do` — emit XDO + leave-offset placeholder. */
case FF_OP_DO:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_XDO);
        ff_heap_compile_int(h, 0);
        _FF_CHECK_MEM();
        if (!ff_cf_push(ff, FF_CF_DO, "do", h->size))
            goto done;
    }
    _FF_NEXT();

/** ( -- )  `?do` — emit XQDO + leave-offset placeholder. */
case FF_OP_QDO:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_heap_t *h = &ff->compiling->heap;
        ff_heap_compile_op(h, FF_OP_XQDO);
        ff_heap_compile_int(h, 0);
        _FF_CHECK_MEM();
        if (!ff_cf_push(ff, FF_CF_DO, "?do", h->size))
            goto done;
    }
    _FF_NEXT();

/** ( -- )  `loop` — emit XLOOP + back-offset, patch leave-target. */
case FF_OP_LOOP:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_pop(ff, FF_CF_DO, "loop", "do");
        if (!c)
            goto done;
        ff_heap_t *h = &ff->compiling->heap;
        size_t bp = c->pos;
        ff_heap_compile_op(h, FF_OP_XLOOP);
        ff_heap_compile_int(h, -(ff_int_t)(h->size - bp));
        _FF_CHECK_MEM();
        h->data[bp - 1] = h->size - bp + 1;
        /* DO leave-target lands here. */
        ff_heap_inhibit_peephole(h);
    }
    _FF_NEXT();

/** ( -- )  `+loop` — emit PXLOOP + back-offset, patch leave-target. */
case FF_OP_PLOOP:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_cf_t *c = ff_cf_pop(ff, FF_CF_DO, "+loop", "do");
        if (!c)
            goto done;
        ff_heap_t *h = &ff->compiling->heap;
        size_t bp = c->pos;
        ff_heap_compile_op(h, FF_OP_PXLOOP);
        ff_heap_compile_int(h, -(ff_int_t)(h->size - bp));
        _FF_CHECK_MEM();
        h->data[bp - 1] = h->size - bp + 1;
        /* DO leave-target lands here. */
        ff_heap_inhibit_peephole(h);
    }
    _FF_NEXT();

/** ( -- )  `abort"` — in a definition, compile a -2 THROW carrying the
    string that follows; at the prompt, raise it at once. Only ever
    parses: what runs later is FF_OP_ABORTQ_RUNTIME, so an immediate word
    that uses `abort"` raises when it runs instead of compiling. */
case FF_OP_ABORTQ:
    _FF_SYNC();
    {
        const char *str = ff_parse(ff, "abort\"", FF_TOKEN_STRING);
        if (!str)
            goto done;
        if (!(ff->state & FF_STATE_COMPILING))
        {
            ff_raise(ff, FF_THROW_ABORTQ, FF_SEV_ERROR | FF_ERR_ABORTED,
                     "%s", str);
            goto done;
        }
        ff_heap_compile_op(&ff->compiling->heap, FF_OP_ABORTQ_RUNTIME);
        ff_heap_compile_str(&ff->compiling->heap, str,
                            (size_t)ff->tokenizer.token_len);
        _FF_CHECK_MEM();
    }
    _FF_NEXT();

/** ( -- )  Runtime of `abort"`: THROW -2 with the inline string as the
    message. Uncaught, it resets the engine like ABORT, and the host gets
    the string as the error message. */
case FF_OP_ABORTQ_RUNTIME:
    _FF_SYNC();
    ff_raise(ff, FF_THROW_ABORTQ, FF_SEV_ERROR | FF_ERR_ABORTED,
             "%s", (const char *)(ip + 1));
    goto done;
