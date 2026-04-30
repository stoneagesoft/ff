/*
 * ff_exec_setup_p.h — shared prologue for both ff_exec() implementations.
 *
 * Included at the top of both the GCC computed-goto version and the
 * MSVC switch version. Contains all variable declarations, the
 * dispatch-entry initialisation, the cached-TOS setup, and every macro
 * that is identical across both implementations.
 *
 * Macros NOT defined here (defined per-implementation instead):
 *   _FF_NEXT()    — "advance ip and dispatch next opcode"
 *   _FF_CASE(op)  — opcode handler label / case keyword
 *
 * This file is NOT a standalone header — don't include it outside
 * ff_exec().
 */

    assert(w);

    ff_stack_t    *S  = &ff->stack;
    ff_stack_t    *R  = &ff->r_stack;
    ff_bt_stack_t *BT = &ff->bt_stack;

    /* Local watchdog batch counter. Initialised before the
       early-goto-done path so the done block's flush is well-defined
       even when no dispatch ran. */
    int wd_tick = FF_WD_BATCH;

    /* Snapshot cur_word so an error-path `goto done` (which bypasses
       any pending EXITs) restores the caller's value. The clean-EXIT
       path also unwinds correctly because every NEST / DOES_RUNTIME
       saves cur_word into the return frame and EXIT pops it back. */
    ff_word_t *prev_cur_word = ff->cur_word;
    ff->cur_word = w;
    int bt_size = BT->top;

    if (ff->state & (FF_STATE_TRACE | FF_STATE_BACKTRACE))
    {
        if (ff->state & FF_STATE_TRACE)
            ff_tracef(ff, FF_SEV_TRACE, "%s \xe2\x86\x92", w->name);
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, w);
    }

    /* Prepare dispatch.
       - Opcoded built-in  → synthesize a tiny bytecode [opcode, (word_ptr),
         FF_OP_EXIT]. Push a NULL return sentinel on R so EXIT terminates
         cleanly.
       - External FF_OP_NONE word with a fn pointer → call it directly
         (it may, for colon-def'd words via ff_w_nest, set ff->ip).
       - Otherwise nothing to run. */
    ff_int_t exec_scratch[3];
    ff_int_t *ip;

    if (w->opcode != FF_OP_NONE)
    {
        exec_scratch[0] = w->opcode;
        int n = 1;
        if (w->opcode == FF_OP_NEST
                || w->opcode == FF_OP_TNEST
                || w->opcode == FF_OP_DOES_RUNTIME
                || w->opcode == FF_OP_CREATE_RUNTIME
                || w->opcode == FF_OP_CONSTANT_RUNTIME
                || w->opcode == FF_OP_ARRAY_RUNTIME
                || w->opcode == FF_OP_DEFER_RUNTIME)
            exec_scratch[n++] = (ff_int_t)(intptr_t)w;
        exec_scratch[n] = FF_OP_EXIT;
        /* Two-cell return frame: [saved_ip, saved_cur_word]. The
           outermost ip is NULL — the EXIT case detects that and goes
           to `done`. The cur_word slot carries the caller's value so
           a nested ff_exec (e.g. via EXECUTE) restores it cleanly. */
        ff_stack_push(R, 0);
        ff_stack_push(R, (ff_int_t)(intptr_t)prev_cur_word);
        ip = exec_scratch;
    }
    else if (w->flags & FF_WORD_NATIVE)
    {
        ff_word_native_fn(w)(ff);
        ip = ff->ip;
    }
    else
    {
        ip = NULL;
    }

    if (!ip)
        goto done;

    /* Top-of-stack register cache. While dispatching, the topmost data
       stack value lives in `tos` (when S->top > 0); the in-memory slot at
       S->data[S->top - 1] is treated as scratch and may be stale until the
       next _SYNC_TOS / _FF_SYNC. This shaves a load+store off every
       arithmetic operation that takes or returns TOS in place. Pure pushes
       still have to write the displaced TOS back, so the optimization
       targets compute-heavy bytecode rather than push-heavy code. */
    ff_int_t tos = S->top
                        ? S->data[S->top - 1]
                        : 0;

    #define _SYNC_TOS()   do { if (S->top) S->data[S->top - 1] = tos; } while (0)
    #define _LOAD_TOS()   do { if (S->top) tos = S->data[S->top - 1]; } while (0)

    #define _FF_SYNC()    do { ff->ip = ip; _SYNC_TOS(); } while (0)
    #define _FF_RESTORE() do { ip = ff->ip; _LOAD_TOS(); } while (0)

    /* In-register convenience accessors used by case bodies. _TOS is the
       cached TOS value (lvalue), _NOS / _SAT(i) reach into memory below
       the cache. _SAT(0) is invalid — use _TOS for index 0. */
    #define _TOS          tos
    #define _NOS          (S->data[S->top - 2])
    #define _SAT(i)       (S->data[S->top - 1 - (i)])

    /* Stack-mutating helpers used by case bodies. _PUSH stores the
       displaced TOS to memory before bringing in the new top; _DROP /
       _DROPN reload TOS from memory if any items remain. */
    #define _PUSH(x) \
        do { \
            if (S->top) S->data[S->top - 1] = tos; \
            tos = (x); \
            ++S->top; \
        } while (0)
    #define _PUSH_PTR(p)  _PUSH((ff_int_t)(intptr_t)(p))
    #define _PUSH_REAL(r) \
        do { \
            if (S->top) S->data[S->top - 1] = tos; \
            ff_set_real(&tos, (r)); \
            ++S->top; \
        } while (0)
    #define _DROP() \
        do { \
            if (--S->top) tos = S->data[S->top - 1]; \
        } while (0)
    #define _DROPN(n) \
        do { \
            S->top -= (n); \
            if (S->top) tos = S->data[S->top - 1]; \
        } while (0)

    /* Dispatch-context validation. Unlike FF_SL/FF_SO/FF_RSL/FF_RSO in
       ff_p.h these `goto done` rather than `return`-ing, because ff_exec
       returns bool and must restore state before returning. */
    #define _FF_SL(n) \
        do { \
            if (ff_unlikely((int)S->top < (int)(n))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER, \
                          "Stack underflow: %d item(s) expected.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_SO(n) \
        do { \
            if (ff_unlikely((int)S->top + (int)(n) > FF_STACK_SIZE)) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_OVER, \
                          "Stack overflow: %d item(s) would not fit.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_RSL(n) \
        do { \
            if (ff_unlikely((int)R->top < (int)(n))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_UNDER, \
                          "Return stack underflow: %d item(s) expected.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_RSO(n) \
        do { \
            if (ff_unlikely((int)R->top + (int)(n) > FF_STACK_SIZE)) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_OVER, \
                          "Return stack overflow: %d item(s) would not fit.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_COMPILING \
        do { \
            if (ff_unlikely(!(ff->state & FF_STATE_COMPILING))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF, \
                          "Compiler word outside definition."); \
                goto done; \
            } \
        } while (0)

    /* Dispatch-context address check; gated by FF_SAFE_MEM. Compiles
       to nothing in the default build. See ff_p.h:FF_CHECK_ADDR for
       the word-fn variant. */
#if FF_SAFE_MEM
    #define _FF_CHECK_ADDR(addr, bytes) \
        do { \
            if (ff_unlikely(!ff_addr_valid(ff, (addr), (size_t)(bytes)))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "Bad pointer: %p (size %zu).", \
                          (const void *)(addr), (size_t)(bytes)); \
                goto done; \
            } \
        } while (0)
    #define _FF_CHECK_XT(w) \
        do { \
            if (ff_unlikely(!ff_word_valid(ff, (w)))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "Bad execution token: %p.", (const void *)(w)); \
                goto done; \
            } \
        } while (0)
#else
    #define _FF_CHECK_ADDR(addr, bytes) ((void)0)
    #define _FF_CHECK_XT(w)             ((void)0)
#endif

    /* Watchdog: count back-branches and word calls, check for an
       async abort, and (every N opcodes) call the host's polling
       watchdog.

       The hot path is a local-counter decrement-and-test — no
       memory write, no atomic load, no flag check on the typical
       tick. Every FF_WD_BATCH ticks (256) we sync the local count
       to ff->opcodes_run, do the atomic abort load, and compare
       against the watchdog threshold.

       Async-abort latency goes from "next opcode" to "next 256
       opcodes" — sub-microsecond on this hardware, and well below
       the documented 65536-default watchdog interval. The
       FF_WD_BATCH constant is defined at file scope below so the
       local `wd_tick` declaration can reference it. */
    #define _FF_WATCHDOG_TICK() \
        do { \
            if (ff_unlikely(--wd_tick <= 0)) \
            { \
                wd_tick = FF_WD_BATCH; \
                ff->opcodes_run += FF_WD_BATCH; \
                if (ff_unlikely(FF_ABORT_LOAD(&ff->abort_requested))) \
                    goto _watchdog_abort; \
                if (ff->platform.watchdog \
                        && ff->opcodes_run >= ff->next_watchdog_at) \
                { \
                    _FF_SYNC(); \
                    ff_watchdog_action_t _wd = \
                        ff->platform.watchdog(ff->platform.context, \
                                              ff->opcodes_run); \
                    _FF_RESTORE(); \
                    uint32_t _step = ff->platform.watchdog_interval \
                                         ? ff->platform.watchdog_interval \
                                         : 65536; \
                    ff->next_watchdog_at = ff->opcodes_run + _step; \
                    if (_wd != FF_WD_CONTINUE) \
                        goto _watchdog_abort; \
                } \
            } \
        } while (0)

    /* Trusted-bytecode return-stack checks. Inside opcodes that the
       compiler emits in matched pairs (XDO ... XLOOP, etc.), the
       _FF_RSL_T / _FF_RSO_T checks guard an engine-bug-only failure
       and can be elided when FF_R_TRUSTED is on. The unsuffixed
       _FF_RSL / _FF_RSO above stay live because they protect words
       (>R, R>, R@) that user code can stand-alone-misuse. */
#if FF_R_TRUSTED
    #define _FF_RSL_T(n)  ((void)0)
    #define _FF_RSO_T(n)  ((void)0)
#else
    #define _FF_RSL_T(n)  _FF_RSL(n)
    #define _FF_RSO_T(n)  _FF_RSO(n)
#endif
