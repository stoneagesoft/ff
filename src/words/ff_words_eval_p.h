/*
 * ff --- eval word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( s -- ec )  `evaluate` — ff_eval() the string at TOS, push the error code. */
case FF_OP_EVALUATE:
    _FF_SL(1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, 1);
    {
        const char *src = (const char *)(intptr_t)tos;
        _FF_DROP();
        ff_int_t *prev_ip = ff->ip;
        _FF_SYNC();
        ff->ip = NULL;
        ff_error_t ec = ff_eval(ff, src);
        ff->ip = prev_ip;
        _FF_RESTORE();
        _FF_SO(1);
        _FF_PUSH((ff_int_t)ec);
    }
    _FF_NEXT();

/** ( -- c-addr )  `parse-word` — parse the next whitespace-delimited token
    from the input and push it as a NUL-terminated C string in the arena.
    At end of input the string is empty (length 0). */
case FF_OP_PARSE_WORD:
    _FF_SYNC();
    {
        ff_token_t tok = ff_tokenizer_next(&ff->tokenizer, ff->input,
                                           &ff->input_pos);
        char *s = ff_pad_intern(ff,
                                tok == FF_TOKEN_NULL ? "" : ff->tokenizer.token,
                                tok == FF_TOKEN_NULL ? 0
                                        : (size_t)ff->tokenizer.token_len);
        _FF_RESTORE();
        if (!s)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_OOM,
                      "Out of memory in parse-word.");
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
        int delim = (int)tos;
        _FF_DROP();
        _FF_SYNC();
        const char *src = ff->input ? ff->input : "";
        int p = ff->input_pos;
        int start = p;
        while (src[p] != '\0' && src[p] != (char)delim)
            p++;
        char *s = ff_pad_intern(ff, src + start, (size_t)(p - start));
        if (src[p] == (char)delim)   /* consume the delimiter if present */
            p++;
        ff->input_pos = p;
        _FF_RESTORE();
        if (!s)
        {
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_OOM, "Out of memory in parse.");
            goto done;
        }
        _FF_SO(1);
        _FF_PUSH_PTR(s);
    }
    _FF_NEXT();

/** ( s -- ec )  `load` — ff_load() the file at TOS, push the error code. */
case FF_OP_LOAD:
    _FF_SL(1);
    _FF_CHECK_ADDR((const void *)(intptr_t)tos, 1);
    {
        const char *path = (const char *)(intptr_t)tos;
        _FF_DROP();
        _FF_SYNC();
        ff_error_t ec = ff_load(ff, path);
        _FF_RESTORE();
        _FF_SO(1);
        _FF_PUSH((ff_int_t)ec);
    }
    _FF_NEXT();
