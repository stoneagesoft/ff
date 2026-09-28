#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- compile-time word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( -- a )  R: ( -- ret cur )  Runtime entry of a DOES>-built word:
    save caller frame, jump to the does-clause, push the data field. */
case FF_OP_DOES_RUNTIME:
    _FF_WATCHDOG_TICK();
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        _FF_RSO(2);
        _FF_SO(1);
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, ff->cur_word);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);
        ff_stack_push(R, (ff_int_t)(intptr_t)ff->cur_word);
        ff->cur_word = nw;
        ip = nw->does;
        _FF_PUSH_PTR(nw->heap.data);
    }
    _FF_NEXT();

/** ( -- )  `{` — open a scope. Pushes a compile-time signature record and
    hands the next tokens to the signature parser in ff_eval, which emits
    FF_OP_SCOPE_ENTER once it reaches the closing `)`. */
case FF_OP_LBRACE:
    _FF_COMPILING;
    if (ff_unlikely(ff->n_csig >= FF_CSCOPE_DEPTH))
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_OVER,
                  "Scopes nested deeper than %d.", FF_CSCOPE_DEPTH);
        goto done;
    }
    {
        ff_csig_t *cs = &ff->csig[ff->n_csig++];
        memset(cs, 0, sizeof(*cs));
        cs->phase = FF_CSIG_EXPECT_OPEN;
        /* `(` is a block comment as far as the tokenizer is concerned;
           signature mode suspends that so the parser sees the tokens. */
        ff->state |= FF_STATE_SIG_PENDING;
        ff->tokenizer.state |= FF_TOK_STATE_SIG;
    }
    _FF_NEXT();

/** ( -- )  `}` — close a scope: emit FF_OP_SCOPE_EXIT and drop the
    compile-time signature record. A control structure opened inside the
    scope must be closed inside it too. */
case FF_OP_RBRACE:
    _FF_COMPILING;
    if (ff_unlikely(ff->n_csig <= 0))
    {
        _FF_SYNC();
        ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "'}' without a matching '{'.");
        goto done;
    }
    if (ff_unlikely(ff->n_cf > 0 && ff->cf[ff->n_cf - 1].scope == ff->n_csig))
    {
        _FF_SYNC();
        ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "'%s' still open at '}'.", ff->cf[ff->n_cf - 1].opener);
        goto done;
    }
    {
        ff_csig_t *cs = &ff->csig[ff->n_csig - 1];
        ff_heap_t *h  = &ff->compiling->heap;

        ff_heap_compile_op(h, FF_OP_SCOPE_EXIT);
        ff_heap_compile_int(h, FF_SCOPE_PACK_EXIT(cs->nargs, cs->nouts,
                                                  cs->var_out));
        _FF_CHECK_MEM();
        /* A scope boundary is a peephole barrier too: the next op must
           not fold with the scope's last instruction. */
        ff_heap_inhibit_peephole(h);

        for (int i = 0; i < cs->nargs; i++)
            free(cs->names[i]);
        ff->n_csig--;
    }
    _FF_NEXT();

/** ( -- )  `immediate` — flag the most recent word as immediate. */
case FF_OP_IMMEDIATE:
    _FF_NEED_DEF;
    ff_dict_top(&ff->dict)->flags |= FF_WORD_IMMEDIATE;
    _FF_NEXT();

/** ( -- )  `[` — switch from compile to interpret mode (immediate). */
case FF_OP_LBRACKET:
    _FF_COMPILING;
    ff->state &= ~FF_STATE_COMPILING;
    _FF_NEXT();

/** ( -- )  `]` — resume compiling the definition that `[` left. */
case FF_OP_RBRACKET:
    if (ff_unlikely(!ff->compiling))
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF,
                  "No definition to resume.");
        goto done;
    }
    ff->state |= FF_STATE_COMPILING;
    _FF_NEXT();

/** ( -- flag )  `state` — push -1 if compiling, 0 if interpreting. */
case FF_OP_STATE:
    _FF_SO(1);
    _FF_PUSH((ff->state & FF_STATE_COMPILING) ? FF_TRUE : FF_FALSE);
    _FF_NEXT();

/** ( -- )  `[']` — compile the xt of the word that follows as a literal. */
case FF_OP_BRACKET_TICK:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_word_t *w = ff_parse_word(ff, "[']");
        if (!w)
            goto done;
        ff_heap_compile_op(&ff->compiling->heap, FF_OP_LIT);
        ff_heap_compile_int(&ff->compiling->heap, (ff_int_t)(intptr_t)w);
        _FF_CHECK_MEM();
    }
    _FF_NEXT();

/** ( -- )  `[compile]` — compile a call to the word that follows, even an
    immediate one. */
case FF_OP_BRACKET_COMPILE:
    _FF_COMPILING;
    _FF_SYNC();
    {
        ff_word_t *w = ff_parse_word(ff, "[compile]");
        if (!w || !ff_compile_call(ff, w))
            goto done;
    }
    _FF_NEXT();

/** ( v -- )  `literal` — pop and compile a literal of that value. */
case FF_OP_LITERAL:
    _FF_COMPILING;
    _FF_SL(1);
    ff_heap_compile_lit(&ff->compiling->heap, tos);
    _FF_CHECK_MEM();
    _FF_DROP();
    _FF_NEXT();

/** ( -- )  `compile` — parse the next word and compile code that, when
    this definition runs, compiles a call to it into the definition then
    in progress: `postpone` for any word, immediate or not. Immediate, and
    parsing — the classic form copied the next compiled cell, which is
    half an instruction for any word that compiles to two. */
case FF_OP_COMPILE:
    _FF_COMPILING;
    _FF_SYNC();
    if (!ff_postpone(ff, "compile", true))
        goto done;
    _FF_NEXT();

/** ( -- )  `postpone` — parse the next word and append its compilation
    semantics to the current definition (see ff_postpone). */
case FF_OP_POSTPONE:
    _FF_COMPILING;
    _FF_SYNC();
    if (!ff_postpone(ff, "postpone", false))
        goto done;
    _FF_NEXT();

/** ( -- )  Runtime of a postponed non-immediate word: compile a call to
    the carried word into whatever definition is currently in progress.
    This is what makes `postpone` defer by one level. */
case FF_OP_POSTPONE_RUNTIME:
    {
        ff_word_t *w = (ff_word_t *)(intptr_t)*ip++;
        _FF_SYNC();
        if (ff_unlikely(!(ff->state & FF_STATE_COMPILING)))
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF,
                      "A postponed word ran with no definition being compiled.");
            goto done;
        }
        if (!ff_compile_call(ff, w))
            goto done;
    }
    _FF_NEXT();

/** ( -- )  `recurse` — compile a call to the definition being compiled.
    A definition can also call itself by name; `recurse` is the standard
    spelling, for code shared with other Forths. */
case FF_OP_RECURSE:
    _FF_COMPILING;
    ff_heap_compile_word(&ff->compiling->heap, ff->compiling);
    _FF_CHECK_MEM();
    _FF_NEXT();

/** ( -- )  `:` — begin a colon definition, named by the next token. */
case FF_OP_COLON:
    if (ff_unlikely(ff->compiling))
    {
        _FF_SYNC();
        ff_raise(ff, FF_THROW_NESTING, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "':' inside the definition of '%s'.", ff->compiling->name);
        goto done;
    }
    _FF_SYNC();
    if (!ff_def_begin(ff))
        goto done;
    _FF_NEXT();

/** ( -- )  `;` — finish a colon-def; emits EXIT or folds to TNEST tail-call. */
case FF_OP_SEMICOLON:
    _FF_COMPILING;
    if (ff_unlikely(ff->n_csig != 0))
    {
        _FF_SYNC();
        ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "Definition ended with %d scope(s) still open.", ff->n_csig);
        goto done;
    }
    if (ff_unlikely(ff->n_cf != 0))
    {
        _FF_SYNC();
        ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "'%s' still open at ';'.", ff->cf[ff->n_cf - 1].opener);
        goto done;
    }
    {
        ff_heap_t *h = &ff->compiling->heap;
        /* Tail-call peephole: if the body ends with [NEST, word_ptr],
           rewrite NEST → TNEST and skip the EXIT emit. The TNEST opcode
           replaces the current frame so the called word's EXIT pops the
           older caller's return address directly.

           data[size - 2] is only an opcode if the trailing instruction
           carries exactly one operand cell, so ask the metadata table
           rather than assuming. Without this a body ending in an opcode
           whose *operand* happens to equal FF_OP_NEST (== 1) would have
           that operand rewritten to FF_OP_TNEST and its EXIT omitted —
           which `{ ( a -- b ) … }` hits immediately, since SCOPE_EXIT's
           packed operand carries nargs == 1 in its low bits. */
        if (h->size >= 2
                && h->data[h->size - 2] == FF_OP_NEST
                && ff_heap_op_starts_at(h, h->size - 2))
            h->data[h->size - 2] = FF_OP_TNEST;
        else
            ff_heap_compile_op(h, FF_OP_EXIT);
        _FF_CHECK_MEM();
        /* Definition is closed — return the unused tail of this
           heap's allocation to the arena. */
        ff_heap_trim(h);
    }
    ff->state &= ~FF_STATE_COMPILING;
    ff_word_set_opcode(ff->compiling, FF_OP_NEST);
    ff->compiling = NULL;
    _FF_NEXT();

/** ( -- xt )  `'` — push the xt of the word that follows. Parsed when
    `'` runs: in a definition, from the input at run time. */
case FF_OP_TICK:
    _FF_SO(1);
    _FF_SYNC();
    {
        ff_word_t *tw = ff_parse_word(ff, "'");
        if (!tw)
            goto done;
        /* Synced section: push via memory; _FF_RESTORE reloads tos. */
        ff_stack_push_ptr(S, tw);
    }
    _FF_RESTORE();
    _FF_NEXT();

/** ( xt -- )  `execute` — run the word identified by xt. Enters the
    word's stub under a return frame, as NEST enters a body: no C
    recursion, so the call depth is bounded by the return stack, and an
    error, THROW or QUIT inside it unwinds through the caller like any
    other. */
case FF_OP_EXECUTE:
    _FF_SL(1);
    _FF_RSO(2);
    {
        ff_word_t *tw = (ff_word_t *)(intptr_t)tos;
        _FF_CHECK_XT(tw);
        _FF_DROP();
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, ff->cur_word);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);
        ff_stack_push(R, (ff_int_t)(intptr_t)ff->cur_word);
        ff->cur_word = tw;
        ip = tw->stub;
    }
    _FF_NEXT();

/**
 * ( -- )  `does>` — install runtime body for the word being defined,
 * then bail out of the definition like an EXIT.
 */
case FF_OP_DOES:
    _FF_NEED_DEF;
    /* does> ends the defining word, whose compiled body holds the
       runtime code that follows it. Run straight from its own stub — at
       the prompt, or through `execute` — there is no such body: the
       created word would get the stub as its runtime, and the frame
       popped below would be the caller's, not a defining word's. */
    if (ff_unlikely(_FF_RUNNING_DIRECT))
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF,
                  "does> used outside a defining word.");
        goto done;
    }
    /* Only a word `create` made: the does> clause replaces its run-time
       action. A colon definition keeps being NESTed into by the code
       that already calls it, so turning it into a data word would leave
       live bytecode writable. */
    if (ff_unlikely(ff_dict_top(&ff->dict)->opcode != FF_OP_CREATE_RUNTIME
                    && ff_dict_top(&ff->dict)->opcode != FF_OP_DOES_RUNTIME))
    {
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF,
                  "does> needs a word made by create; '%s' isn't one.",
                  ff_dict_top(&ff->dict)->name);
        goto done;
    }
    _FF_RSL_T(2);
    ff_dict_top(&ff->dict)->does = ip;
    ff_word_set_opcode(ff_dict_top(&ff->dict), FF_OP_DOES_RUNTIME);
    /* Simulate EXIT to bail out of the definition: pop the 2-cell
       return frame (cur_word on top, ip below). */
    ff->cur_word = (ff_word_t *)(intptr_t)*ff_tos(R);
    R->top--;
    ip = (ff_int_t *)(intptr_t)*ff_tos(R);
    R->top--;
    if (BT->top > 0)
        BT->top--;
    if (!ip)
        goto done;
    _FF_NEXT();
