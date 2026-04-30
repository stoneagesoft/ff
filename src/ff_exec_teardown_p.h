/*
 * ff_exec_teardown_p.h — shared exit labels and macro cleanup for
 * both ff_exec() implementations.
 *
 * Included at the bottom of both the GCC computed-goto version and
 * the MSVC switch version, after the dispatch body.
 *
 * This file is NOT a standalone header — don't include it outside
 * ff_exec().
 */

_watchdog_abort:
    /* Watchdog or async ff_request_abort fired. Surface it as a
       FF_SEV_ERROR | FF_ERR_ABORTED, clear the flag (so the next
       ff_eval call starts fresh), and join the broken-state cleanup
       path. */
    _FF_SYNC();
    ff_tracef(ff, FF_SEV_ERROR | FF_ERR_ABORTED,
              "Aborted after %llu opcodes.",
              (unsigned long long)ff->opcodes_run);
    FF_ABORT_CLEAR(&ff->abort_requested);
    ff->state |= FF_STATE_BROKEN;
    goto broken;

broken:
    /* Flush the local watchdog batch counter back into the engine's
       running total before returning, so a host that reads
       ff->opcodes_run after a failed run sees an accurate count. */
    ff->opcodes_run += (uint64_t)(FF_WD_BATCH - wd_tick);
    ff->ip = ip;
    if (S->top) S->data[S->top - 1] = tos;
    ff->cur_word = prev_cur_word;
    BT->top = bt_size;
    return false;

done:
    ff->opcodes_run += (uint64_t)(FF_WD_BATCH - wd_tick);
    ff->ip = ip;
    if (S->top) S->data[S->top - 1] = tos;
    ff->cur_word = prev_cur_word;
    BT->top = bt_size;
    return true;

    #undef _FF_NEXT
    #undef _FF_CASE
    #undef _FF_SYNC
    #undef _FF_RESTORE
    #undef _SYNC_TOS
    #undef _LOAD_TOS
    #undef _TOS
    #undef _NOS
    #undef _SAT
    #undef _PUSH
    #undef _PUSH_PTR
    #undef _PUSH_REAL
    #undef _DROP
    #undef _DROPN
    #undef _FF_SL
    #undef _FF_SO
    #undef _FF_RSL
    #undef _FF_RSO
    #undef _FF_RSL_T
    #undef _FF_RSO_T
    #undef _FF_COMPILING
    #undef _FF_CHECK_ADDR
    #undef _FF_CHECK_XT
    #undef _FF_WATCHDOG_TICK
