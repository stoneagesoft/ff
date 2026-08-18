#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- scope word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * Run-time half of `{ ( a b -- c ) … }`. The compile-time half (parsing
 * the signature, resolving names to FF_OP_ARG) lives in ff.c and
 * ff_words_comp_p.h.
 *
 * Stack layout while a scope is open, growing upward:
 *
 *     … caller's cells … | a b c | scope's own working cells
 *                        ^       ^
 *                        |       floor  — _FF_SL sees only cells above this
 *                        floor - nargs
 *
 * The inputs sit *below* the barrier, unreachable except through their
 * names, and `}` slides the outputs down over them.
 */

/** ( … -- )  Install the barrier. Operand: FF_SCOPE_PACK_ENTER(nargs, inherit). */
case FF_OP_SCOPE_ENTER:
    {
        ff_int_t packed = *ip++;
        int      nargs  = FF_SCOPE_NARGS(packed);

        /* The caller must actually own `nargs` cells *within the
           enclosing scope*. Checked against the old floor, before it
           moves — otherwise the barrier's own prologue would be the
           thing that reaches past the enclosing barrier. */
        if (ff_unlikely((int)(S->top - floor) < nargs))
        {
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                      "Stack underflow: scope expected %d input(s).", nargs);
            goto done;
        }
        if (ff_unlikely(ff->n_scopes >= FF_SCOPE_DEPTH))
        {
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_OVER,
                      "Too many open scopes (max %d).", FF_SCOPE_DEPTH);
            goto done;
        }

        ff->scopes[ff->n_scopes].floor = (uint32_t)floor;
        ff->scopes[ff->n_scopes].r_top = (uint32_t)R->top;
        ff->n_scopes++;

        /* `( ... -- ... )` inherits the enclosing barrier rather than
           installing a fresh one: an explicitly-unchecked scope still
           cannot reach past the scope that contains it. */
        if (!FF_SCOPE_INHERIT(packed))
        {
            /* TOS is register-cached; the barrier makes the cells below
               it addressable only via FF_OP_ARG, which reads memory. */
            _FF_SYNC_TOS();
            floor = S->top;
            /* Write both copies. `floor` is read on every _FF_SL but
               written only here and at SCOPE_EXIT, so keeping the two
               permanently coherent costs nothing and avoids leaving a
               stale barrier in memory for the next ff_exec to pick up. */
            S->floor = floor;
        }
    }
    _FF_NEXT();

/** ( -- x )  Push a named input. Operand: k, the depth below the barrier (1 = last-declared). */
case FF_OP_ARG:
    _FF_SO(1);
    {
        ff_int_t k = *ip++;
        _FF_PUSH(S->data[floor - (size_t)k]);
    }
    _FF_NEXT();

/** ( … -- … )  Check arity, slide outputs over inputs, restore the barrier.
    Operand: FF_SCOPE_PACK_EXIT(nargs, nouts, varout). */
case FF_OP_SCOPE_EXIT:
    {
        ff_int_t packed = *ip++;
        int      nargs  = FF_SCOPE_NARGS(packed);
        int      nouts  = FF_SCOPE_NOUTS(packed);

        if (ff_unlikely(ff->n_scopes == 0))
        {
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BROKEN,
                      "Scope exit with no open scope.");
            goto done;
        }

        _FF_SYNC_TOS();

        size_t produced = S->top - floor;

        /* The promise in `-- c` is the whole point: check it before the
           slide, so a broken scope is reported where it broke rather
           than corrupting the caller. */
        if (!FF_SCOPE_VAROUT(packed) && ff_unlikely((int)produced != nouts))
        {
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_ARITY,
                      "Scope declared %d output(s) but left %d.",
                      nouts, (int)produced);
            goto done;
        }

        ff->n_scopes--;
        if (ff_unlikely(R->top != (size_t)ff->scopes[ff->n_scopes].r_top))
        {
            _FF_SYNC();
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_RSTACK,
                      "Scope left the return stack unbalanced "
                      "(depth %d, expected %d).",
                      (int)R->top, (int)ff->scopes[ff->n_scopes].r_top);
            goto done;
        }

        /* Slide the produced cells down over the consumed inputs. */
        if (nargs > 0 && produced > 0)
            memmove(&S->data[floor - (size_t)nargs],
                    &S->data[floor],
                    produced * sizeof(S->data[0]));

        S->top   = floor - (size_t)nargs + produced;
        floor    = ff->scopes[ff->n_scopes].floor;
        S->floor = floor;   /* see SCOPE_ENTER: both copies stay coherent */
        _FF_LOAD_TOS();
    }
    _FF_NEXT();
