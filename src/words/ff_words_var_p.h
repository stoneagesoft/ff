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

/** ( -- )  `forget` — mark next token to be removed via ff_dict_forget(). */
case FF_OP_FORGET:
    ff->state |= FF_STATE_FORGET_PENDING;
    _FF_NEXT();

/** ( -- )  `create` — start a new no-data definition; next token names it. */
case FF_OP_CREATE:
    _FF_SYNC();
    if (!ff_def_new(ff, FF_OP_CREATE_RUNTIME))
        goto done;
    _FF_NEXT();

/** ( -- )  `variable` — like CREATE but reserves one cell. */
case FF_OP_VARIABLE:
    _FF_SYNC();
    {
        ff_word_t *nw = ff_def_new(ff, FF_OP_CREATE_RUNTIME);
        if (!nw)
            goto done;
        ff_heap_compile_int(&nw->heap, 0);
        _FF_CHECK_MEM();
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
        ff_word_t *nw = ff_def_new(ff, FF_OP_CONSTANT_RUNTIME);
        if (!nw)
            goto done;
        ff_heap_compile_int(&nw->heap, tos);
        _FF_CHECK_MEM();
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
        ff_word_t *nw = ff_def_new(ff, FF_OP_DEFER_RUNTIME);
        if (!nw)
            goto done;
        /* Reserve a single cell holding the target xt; NULL until `is` sets
           it. The xt slot is mutated by `is` later but the cell count is
           fixed, so the trim is safe. */
        ff_heap_compile_int(&nw->heap, 0);
        _FF_CHECK_MEM();
        ff_heap_trim(&nw->heap);
    }
    _FF_NEXT();

/** ( xt -- )  `is` — store xt into the next-token-named deferred word. */
case FF_OP_IS:
    _FF_SL(1);
    ff->state |= FF_STATE_IS_PENDING;
    _FF_NEXT();
