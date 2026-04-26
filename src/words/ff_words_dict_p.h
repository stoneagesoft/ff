/*
 * ff --- dictionary introspection word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 *
 * The bodies are large (fort tables, dict iteration), so each case
 * delegates to a non-static helper defined in ff_words_dict.c.
 */

/** ( -- )  `words` — list every dictionary entry. */
case FF_OP_WORDS:
    _FF_SYNC();
    ff_print_words(ff, 0);
    _FF_NEXT();

/** ( -- )  `wordsused` — list dictionary entries that have been looked up. */
case FF_OP_WORDSUSED:
    _FF_SYNC();
    ff_print_words(ff, 1);
    _FF_NEXT();

/** ( -- )  `wordsunused` — list dictionary entries never looked up. */
case FF_OP_WORDSUNUSED:
    _FF_SYNC();
    ff_print_words(ff, 2);
    _FF_NEXT();

/** ( -- )  `man` — print manual entry for the next-token word. */
case FF_OP_MAN:
    _FF_SYNC();
    ff_w_man_impl(ff);
    _FF_NEXT();

/** ( -- )  `dump-word` — print metadata + raw heap of next-token word. */
case FF_OP_DUMP_WORD:
    _FF_SYNC();
    ff_w_dump_word_impl(ff);
    _FF_NEXT();

/** ( -- )  `see` — decompile next-token word back to Forth syntax. */
case FF_OP_SEE:
    _FF_SYNC();
    ff_w_see_impl(ff);
    _FF_NEXT();
