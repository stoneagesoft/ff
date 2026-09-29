#ifndef FF_IN_EXEC
#error "This is a dispatch fragment #included inside ff_exec(); do not include it directly."
#endif

/*
 * ff --- eval word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( s -- n )  `evaluate` — ff_eval() the string at TOS. Like `catch`
    around the evaluation: an exception inside it — an error, THROW,
    ABORT — ends the evaluation and its THROW code is pushed (0 when it
    ran to the end). Only the host's abort and `quit` keep unwinding. */
case FF_OP_EVALUATE:
    _FF_SL(1);
    _FF_CHECK_STR(tos);
    {
        const char *src = (const char *)(intptr_t)tos;
        _FF_DROP();
        _FF_SYNC();
        /* The string is an input of its own: a `(` comment still open at
           its end ends there, instead of swallowing the rest of the
           caller's source (only the host's line-by-line input carries a
           comment over to its next line). */
        int comment = ff->tokenizer.state & FF_TOK_STATE_COMMENT;
        ff->tokenizer.state &= ~FF_TOK_STATE_COMMENT;
        ff_error_t ec = ff_eval(ff, src);
        ff->tokenizer.state = (ff->tokenizer.state & ~FF_TOK_STATE_COMMENT)
                              | comment;
        _FF_RESTORE();
        _FF_CHECK_THROWN();
        _FF_SO(1);
        _FF_PUSH(ec == FF_OK ? 0 : ff->throw_code);
    }
    _FF_NEXT();

/** ( -- c-addr )  `parse-word` — parse the next whitespace-delimited token
    from the input and push it as a NUL-terminated C string in the arena.
    At end of input the string is empty (length 0). */
case FF_OP_PARSE_WORD:
    _FF_SYNC();
    {
        ff_token_t tok = ff->input
                             ? ff_tokenizer_next(&ff->tokenizer, ff->input,
                                                 &ff->input_pos)
                             : FF_TOKEN_NULL;
        char *s = ff_pad_intern(ff,
                                tok == FF_TOKEN_NULL ? "" : ff->tokenizer.token,
                                tok == FF_TOKEN_NULL ? 0
                                        : (size_t)ff->tokenizer.token_len);
        _FF_RESTORE();
        if (!s)
        {
            ff_mem_check(ff);
            goto done;
        }
        _FF_SO(1);
        _FF_PUSH_PTR(s);
    }
    _FF_NEXT();

/** ( char -- c-addr )  `parse` — parse text from the current input position
    up to (and consuming) the next occurrence of delimiter *char*, and push
    it as a C string. Leading delimiters are NOT skipped, so two `parse`s in
    a row can return empty strings — this is standard `parse` behaviour. */
case FF_OP_PARSE:
    _FF_SL(1);
    {
        /* The delimiter is the cell's low byte. */
        char delim = (char)(unsigned char)tos;
        _FF_DROP();
        _FF_SYNC();
        const char *src = ff->input ? ff->input : "";
        int p = ff->input_pos;
        int start = p;
        while (src[p] != '\0' && src[p] != delim)
            p++;
        char *s = ff_pad_intern(ff, src + start, (size_t)(p - start));
        /* Consume the delimiter if the scan stopped at one — but never the
           input's terminator: with a delimiter of 0 (or 256, …) that
           matched too, and the next read ran on past the end. */
        if (src[p] != '\0')
            p++;
        ff->input_pos = p;
        _FF_RESTORE();
        if (!s)
        {
            ff_mem_check(ff);
            goto done;
        }
        _FF_SO(1);
        _FF_PUSH_PTR(s);
    }
    _FF_NEXT();

#if FF_WITH_FILES
/** ( s -- n )  `load` — ff_load() the file at TOS, push the THROW code
    of whatever ended it early (0 when it loaded completely), as
    `evaluate` does. */
case FF_OP_LOAD:
    _FF_NEED_CAP(FF_CAP_LOAD, "load");
    _FF_SL(1);
    _FF_CHECK_STR(tos);
    {
        const char *path = (const char *)(intptr_t)tos;
        _FF_DROP();
        _FF_SYNC();
        ff_error_t ec = ff_load(ff, path);
        _FF_RESTORE();
        _FF_CHECK_THROWN();
        _FF_SO(1);
        _FF_PUSH(ec == FF_OK ? 0 : ff->throw_code);
    }
    _FF_NEXT();
#endif
