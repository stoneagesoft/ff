#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- variable / constant / create-runtime dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * Runtime cases (FF_OP_CREATE_RUNTIME, FF_OP_CONSTANT_RUNTIME) are
 * emitted by the compiler for words built via `create` / `variable` /
 * `constant`. Each carries a word pointer as its second cell so the
 * case can access that word's heap.
 */

/** ( -- a )  Runtime entry for a CREATE-built word: push its heap pointer. */
case FF_OP_CREATE_RUNTIME:
    _FF_SO(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        _FF_PUSH_PTR(nw->heap.data);
    }
    _FF_NEXT();

/** ( -- v )  Peephole superinstruction: `v @` → push the value at
    the variable's parameter field directly, no intermediate
    address-on-stack round-trip. Emitted when CREATE_RUNTIME is
    immediately followed by FETCH. */
case FF_OP_VAR_FETCH:
    _FF_SO(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        _FF_PUSH(nw->heap.data[0]);
    }
    _FF_NEXT();

/** ( v -- )  Peephole superinstruction: `v !` → store TOS at the
    variable's parameter field. */
case FF_OP_VAR_STORE:
    _FF_SL(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        nw->heap.data[0] = tos;
        _FF_DROP();
    }
    _FF_NEXT();

/** ( delta -- )  Peephole superinstruction: `v +!`. */
case FF_OP_VAR_PLUS_STORE:
    _FF_SL(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        nw->heap.data[0] += tos;
        _FF_DROP();
    }
    _FF_NEXT();

/** ( -- v )  Runtime entry for a CONSTANT-built word: push the stored value. */
case FF_OP_CONSTANT_RUNTIME:
    _FF_SO(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        _FF_PUSH(nw->heap.data[0]);
    }
    _FF_NEXT();

/** ( -- )  `forget` — remove the word that follows and every later one,
    unless one of them is running: its code would be freed under it. */
case FF_OP_FORGET:
    _FF_SYNC();
    {
        const char *name = ff_parse(ff, "forget", FF_TOKEN_WORD);
        if (!name)
            goto done;
        if (ff->compiling)
        {
            /* It would cut the open definition out from under the
               compiler too. */
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FORGET_PROT,
                      "Can't forget while '%s' is being defined.",
                      ff->compiling->name);
            goto done;
        }
        size_t at = ff_dict_index(&ff->dict, name);
        if (at == (size_t)-1)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                      "'%s' undefined.", name);
            goto done;
        }
        const ff_word_t *running = ff_word_running_from(ff, at);
        if (running)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FORGET_PROT,
                      "Can't forget '%s' while '%s' is running.",
                      name, running->name);
            goto done;
        }
        ff_dict_truncate(&ff->dict, at);
    }
    _FF_NEXT();

/** ( -- )  `create` — make a data word named by the token that follows. */
case FF_OP_CREATE:
    _FF_SYNC();
    if (!ff_def_new(ff, "create", FF_OP_CREATE_RUNTIME))
        goto done;
    _FF_NEXT();

/** ( -- )  `variable` — like CREATE but reserves one cell. */
case FF_OP_VARIABLE:
    _FF_SYNC();
    {
        ff_word_t *nw = ff_def_new(ff, "variable", FF_OP_CREATE_RUNTIME);
        if (!nw)
            goto done;
        ff_heap_compile_int(&nw->heap, 0);
        _FF_CHECK_MEM_NEW(nw);
        /* The variable's heap is exactly one cell — trim the doubling
           overhead from the initial allocation. */
        ff_heap_trim(&nw->heap);
    }
    _FF_NEXT();

/** ( v -- )  `constant` — define a word whose runtime pushes v. */
case FF_OP_CONSTANT:
    _FF_SL(1);
    _FF_SYNC();
    {
        ff_word_t *nw = ff_def_new(ff, "constant", FF_OP_CONSTANT_RUNTIME);
        if (!nw)
            goto done;
        ff_heap_compile_int(&nw->heap, tos);
        _FF_CHECK_MEM_NEW(nw);
        ff_heap_trim(&nw->heap);
    }
    _FF_DROP();
    _FF_NEXT();

/** ( -- )  Runtime entry for a DEFER-built word: call through stored xt,
    exactly as `execute` does — the target's stub runs under a return
    frame, with no C recursion. */
case FF_OP_DEFER_RUNTIME:
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        ff_word_t *target = (ff_word_t *)(intptr_t)nw->heap.data[0];
        if (target == NULL)
        {
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR,
                      "Deferred word '%s' has no action assigned.", nw->name);
            goto done;
        }
        _FF_CHECK_XT(target);
        _FF_RSO(2);
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, ff->cur_word);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);
        ff_stack_push(R, (ff_int_t)(intptr_t)ff->cur_word);
        ff->cur_word = target;
        ip = target->stub;
    }
    _FF_NEXT();

/** ( -- )  `defer` — create a deferred word with no action; next token names it. */
case FF_OP_DEFER:
    _FF_SYNC();
    {
        ff_word_t *nw = ff_def_new(ff, "defer", FF_OP_DEFER_RUNTIME);
        if (!nw)
            goto done;
        /* Reserve a single cell holding the target xt; NULL until `is` sets
           it. The xt slot is mutated by `is` later but the cell count is
           fixed, so the trim is safe. */
        ff_heap_compile_int(&nw->heap, 0);
        _FF_CHECK_MEM_NEW(nw);
        ff_heap_trim(&nw->heap);
    }
    _FF_NEXT();

/** ( xt -- )  `is` — make the deferred word that follows call xt
    (ANS 6.2.1725). Immediate and state-smart, as in ANS: in a definition
    the name is read while compiling, and the definition stores the xt it
    finds on the stack when it runs (FF_OP_IS_RUNTIME). The xt is checked
    when the deferred word runs. */
case FF_OP_IS:
    _FF_SYNC();
    {
        bool compiling = (ff->state & FF_STATE_COMPILING) != 0;
        if (!compiling)
            _FF_SL(1);
        ff_word_t *w = ff_parse_word(ff, "is");
        if (!w)
            goto done;
        if (w->opcode != FF_OP_DEFER_RUNTIME)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNSUPPORTED,
                      "'%s' is not a deferred word.", w->name);
            goto done;
        }
        if (compiling)
        {
            ff_heap_compile_int(&ff->compiling->heap, FF_OP_IS_RUNTIME);
            ff_heap_compile_int(&ff->compiling->heap, (ff_int_t)(intptr_t)w);
            _FF_CHECK_MEM();
        }
        else
        {
            w->heap.data[0] = tos;
            _FF_DROP();
        }
    }
    _FF_NEXT();

/** ( xt -- )  Runtime of `is` in a definition: make the deferred word in
    the operand call xt. */
case FF_OP_IS_RUNTIME:
    _FF_SL(1);
    {
        ff_word_t *w = (ff_word_t *)(intptr_t)*ip++;
        w->heap.data[0] = tos;
    }
    _FF_DROP();
    _FF_NEXT();
