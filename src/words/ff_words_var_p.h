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
        ff->cur_word = nw;
        _PUSH_PTR(nw->heap.data);
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
        _PUSH(nw->heap.data[0]);
    }
    _FF_NEXT();

/** ( v -- )  Peephole superinstruction: `v !` → store TOS at the
    variable's parameter field. */
case FF_OP_VAR_STORE:
    _FF_SL(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        nw->heap.data[0] = tos;
        _DROP();
    }
    _FF_NEXT();

/** ( delta -- )  Peephole superinstruction: `v +!`. */
case FF_OP_VAR_PLUS_STORE:
    _FF_SL(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        nw->heap.data[0] += tos;
        _DROP();
    }
    _FF_NEXT();

/** ( -- v )  Runtime entry for a CONSTANT-built word: push the stored value. */
case FF_OP_CONSTANT_RUNTIME:
    _FF_SO(1);
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        ff->cur_word = nw;
        _PUSH(nw->heap.data[0]);
    }
    _FF_NEXT();

/** ( -- )  `forget` — mark next token to be removed via ff_dict_forget(). */
case FF_OP_FORGET:
    ff->state |= FF_STATE_FORGET_PENDING;
    _FF_NEXT();

/** ( -- )  `create` — start a new no-data definition; next token names it. */
case FF_OP_CREATE:
    ff->state |= FF_STATE_DEF_PENDING;
    ff_dict_append(&ff->dict,
                   ff_word_new(" ", NULL, FF_OP_CREATE_RUNTIME, NULL));
    _FF_NEXT();

/** ( -- )  `variable` — like CREATE but reserves one cell. */
case FF_OP_VARIABLE:
    ff->state |= FF_STATE_DEF_PENDING;
    ff_dict_append(&ff->dict,
                   ff_word_new(" ", NULL, FF_OP_CREATE_RUNTIME, NULL));
    ff_heap_compile_int(&ff_dict_top(&ff->dict)->heap, 0);
    _FF_NEXT();

/** ( v -- )  `constant` — define a word whose runtime pushes v. */
case FF_OP_CONSTANT:
    _FF_SL(1);
    ff->state |= FF_STATE_DEF_PENDING;
    ff_dict_append(&ff->dict,
                   ff_word_new(" ", NULL, FF_OP_CONSTANT_RUNTIME, NULL));
    ff_heap_compile_int(&ff_dict_top(&ff->dict)->heap, tos);
    _DROP();
    _FF_NEXT();

/** ( -- )  Runtime entry for a DEFER-built word: call through stored xt. */
case FF_OP_DEFER_RUNTIME:
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        ff->cur_word = nw;
        ff_word_t *target = (ff_word_t *)(intptr_t)nw->heap.data[0];
        if (target == NULL)
        {
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR,
                      "Deferred word '%s' has no action assigned.", nw->name);
            goto done;
        }
        _FF_SYNC();
        ff_exec(ff, target);
        _FF_RESTORE();
        if (!ip)
            goto done;
    }
    _FF_NEXT();

/** ( -- )  `defer` — create a deferred word with no action; next token names it. */
case FF_OP_DEFER:
    ff->state |= FF_STATE_DEF_PENDING;
    ff_dict_append(&ff->dict,
                   ff_word_new(" ", NULL, FF_OP_DEFER_RUNTIME, NULL));
    /* Reserve a single cell holding the target xt; NULL until `is` sets it. */
    ff_heap_compile_int(&ff_dict_top(&ff->dict)->heap, 0);
    _FF_NEXT();

/** ( xt -- )  `is` — store xt into the next-token-named deferred word. */
case FF_OP_IS:
    _FF_SL(1);
    ff->state |= FF_STATE_IS_PENDING;
    _FF_NEXT();
