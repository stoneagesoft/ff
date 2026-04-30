/*
 * ff --- compile-time word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( -- a )  R: ( -- ret cur )  Runtime entry of a DOES>-built word:
    save caller frame, jump to the does-clause, push the data field. */
case FF_OP_DOES_RUNTIME:
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        _FF_RSO_T(2);
        _FF_SO(1);
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, ff->cur_word);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);
        ff_stack_push(R, (ff_int_t)(intptr_t)ff->cur_word);
        ff->cur_word = nw;
        ip = nw->does;
        _PUSH_PTR(nw->heap.data);
    }
    _FF_NEXT();

/** ( -- )  `immediate` — flag the most recent definition as immediate. */
case FF_OP_IMMEDIATE:
    ff_dict_top(&ff->dict)->flags |= FF_WORD_IMMEDIATE;
    _FF_NEXT();

/** ( -- )  `[` — switch from compile to interpret mode (immediate). */
case FF_OP_LBRACKET:
    _FF_COMPILING;
    ff->state &= ~FF_STATE_COMPILING;
    _FF_NEXT();

/** ( -- )  `]` — switch from interpret to compile mode. */
case FF_OP_RBRACKET:
    ff->state |= FF_STATE_COMPILING;
    _FF_NEXT();

/** ( -- flag )  `state` — push -1 if compiling, 0 if interpreting. */
case FF_OP_STATE:
    _FF_SO(1);
    _PUSH((ff->state & FF_STATE_COMPILING) ? FF_TRUE : FF_FALSE);
    _FF_NEXT();

/** ( -- )  `[']` — anticipate next-token tick (compile-time literal address). */
case FF_OP_BRACKET_TICK:
    _FF_COMPILING;
    ff->state |= FF_STATE_CTICK_PENDING;
    _FF_NEXT();

/** ( -- )  `[compile]` — compile next word non-immediate. */
case FF_OP_BRACKET_COMPILE:
    _FF_COMPILING;
    ff->state |= FF_STATE_CBRACK_PENDING;
    _FF_NEXT();

/** ( v -- )  `literal` — pop and compile a literal of that value. */
case FF_OP_LITERAL:
    _FF_COMPILING;
    _FF_SL(1);
    ff_heap_compile_lit(&ff_dict_top(&ff->dict)->heap, tos);
    _DROP();
    _FF_NEXT();

/** ( -- )  `compile` — compile the next inline cell verbatim. */
case FF_OP_COMPILE:
    _FF_COMPILING;
    ff_heap_compile_int(&ff_dict_top(&ff->dict)->heap, *ip++);
    _FF_NEXT();

/** ( -- )  `:` — start a new colon-def; placeholder name is renamed by next token. */
case FF_OP_COLON:
    ff->state |= FF_STATE_COMPILING | FF_STATE_DEF_PENDING;
    ff_dict_append(&ff->dict,
                   ff_word_new(" ", NULL, FF_OP_NONE, NULL));
    _FF_NEXT();

/** ( -- )  `;` — finish a colon-def; emits EXIT or folds to TNEST tail-call. */
case FF_OP_SEMICOLON:
    _FF_COMPILING;
    {
        ff_heap_t *h = &ff_dict_top(&ff->dict)->heap;
        /* Tail-call peephole: if the body ends with [NEST, word_ptr],
           rewrite NEST → TNEST and skip the EXIT emit. The TNEST opcode
           replaces the current frame so the called word's EXIT pops the
           older caller's return address directly. */
        if (h->size >= 2 && h->data[h->size - 2] == FF_OP_NEST)
            h->data[h->size - 2] = FF_OP_TNEST;
        else
            ff_heap_compile_op(h, FF_OP_EXIT);
    }
    ff->state &= ~FF_STATE_COMPILING;
    ff_dict_top(&ff->dict)->opcode = FF_OP_NEST;
    _FF_NEXT();

/** ( -- xt )  `'` — read next word, push its xt (or defer across input lines). */
case FF_OP_TICK:
    _FF_SO(1);
    _FF_SYNC();
    {
        ff_token_t tok = ff_tokenizer_next(&ff->tokenizer, ff->input, &ff->input_pos);
        if (tok == FF_TOKEN_WORD)
        {
            const ff_word_t *tw = ff_dict_lookup(&ff->dict, ff->tokenizer.token);
            if (tw)
            {
                /* Synced section: push via memory; _FF_RESTORE reloads tos. */
                ff_stack_push_ptr(S, tw);
            }
            else
            {
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                          "'%s' undefined.", ff->tokenizer.token);
                goto done;
            }
        }
        else if (tok == FF_TOKEN_NULL)
        {
            /* No token on current line. If we're at top-level interpret mode,
               defer to the next input line. */
            if (ip >= &exec_scratch[0] && ip <= &exec_scratch[3])
            {
                ff->state |= FF_STATE_TICK_PENDING;
            }
            else
            {
                ff_tracef(ff, FF_ERR_MALFORMED,
                          "Word requested by ' not on same input line.");
                ff_abort(ff);
                goto done;
            }
        }
        else
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MISSING,
                      "Word not specified when expected.");
            ff_abort(ff);
            goto done;
        }
    }
    _FF_RESTORE();
    _FF_NEXT();

/** ( xt -- )  `execute` — recursively run the word identified by xt.
    ff_exec sets ff->ip to NULL on its way out (the sentinel that
    terminates an interpreter run); we save the outer ip across the
    nested call so the caller resumes at the next opcode. */
case FF_OP_EXECUTE:
    _FF_SL(1);
    {
        ff_word_t *tw = (ff_word_t *)(intptr_t)tos;
        _FF_CHECK_XT(tw);
        _DROP();
        _FF_SYNC();
        ff_int_t *saved_ip = ip;
        ff_exec(ff, tw);
        ff->ip = saved_ip;
        _FF_RESTORE();
    }
    _FF_NEXT();

/**
 * ( -- )  `does>` — install runtime body for the word being defined,
 * then bail out of the definition like an EXIT.
 */
case FF_OP_DOES:
    _FF_RSL_T(2);
    ff_dict_top(&ff->dict)->does = ip;
    ff_dict_top(&ff->dict)->opcode = FF_OP_DOES_RUNTIME;
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
