#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- dispatch cases of the words for the language of ff's texts.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * The words run seldom, so each case delegates to a helper in
 * ff_words_tr.c and keeps the dispatch loop small.
 */

/** ( -- s )  `lang@` — a copy of the engine's language code. */
case FF_OP_LANG_FETCH:
    _FF_SYNC();
    ff_w_lang_fetch_impl(ff);
    _FF_RESTORE();
    _FF_CHECK_THROWN();
    _FF_NEXT();

/** ( s -- )  `lang!` — set the engine's language; -24 if @p s names none. */
case FF_OP_LANG_STORE:
    _FF_SYNC();
    ff_w_lang_store_impl(ff);
    _FF_RESTORE();
    _FF_CHECK_THROWN();
    _FF_NEXT();

/** ( -- )  `.langs` — list the languages ff has texts in. */
case FF_OP_DOT_LANGS:
    _FF_SYNC();
    ff_w_dot_langs_impl(ff);
    _FF_NEXT();
