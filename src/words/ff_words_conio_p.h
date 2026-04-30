/*
 * ff --- console I/O word dispatch cases.
 *
 * This header is included inside the `switch (*ip++)` in ff_exec().
 * It is NOT a standalone header — don't include it elsewhere.
 */

/** ( n -- )  `.` — print TOS as integer in current base. */
case FF_OP_DOT:
    _FF_SL(1);
    _FF_SYNC();
    ff_printf(ff, ff->base == FF_BASE_HEX ? "0x%lX" : "%ld", (long)tos);
    _DROP();
    _FF_NEXT();

/** ( a -- )  `?` — print value at address TOS. */
case FF_OP_QUESTION:
    _FF_SL(1);
    _FF_SYNC();
    ff_printf(ff, ff->base == FF_BASE_HEX ? "0x%lX" : "%ld",
              (long)*(ff_int_t *)(intptr_t)tos);
    _DROP();
    _FF_NEXT();

/** ( -- )  `cr` — emit a newline. */
case FF_OP_CR:
    _FF_SYNC();
    ff_printf(ff, "\n");
    _FF_NEXT();

/** ( ch -- )  `emit` — print TOS as a single byte. */
case FF_OP_EMIT:
    _FF_SL(1);
    _FF_SYNC();
    ff_printf(ff, "%c", (char)tos);
    _DROP();
    _FF_NEXT();

/** ( s -- )  `type` — print NUL-terminated string at TOS. */
case FF_OP_TYPE:
    _FF_SL(1);
    _FF_SYNC();
    ff_printf(ff, "%s", (const char *)(intptr_t)tos);
    _DROP();
    _FF_NEXT();

/** ( -- )  `.s` — print the entire data stack as a table. */
case FF_OP_DOT_S:
    if (S->top != 0)
    {
        /* Sync TOS to memory so the loop below reads a coherent stack. */
        _FF_SYNC();
        ft_table_t *tbl = ft_create_table();
        ft_set_border_style(tbl, FT_SOLID_ROUND_STYLE);
        ft_set_cell_prop(tbl, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
        ft_set_cell_prop(tbl, 0, FT_ANY_COLUMN, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
        ft_set_cell_prop(tbl, FT_ANY_ROW, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
        ft_set_cell_prop(tbl, FT_ANY_ROW, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
        ft_set_cell_prop(tbl, FT_ANY_ROW, 4, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
        ft_set_cell_prop(tbl, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
        ft_u8write_ln(tbl, "#", "Dec", "Hex", "Real", "ASCII", "Ptr");
        for (size_t n = 0; n < S->top; ++n)
        {
            ff_int_t v = S->data[n];
            ff_real_t r;
            memcpy(&r, &v, sizeof(r));
            char c = (v > 0 && v < 0xFF && isprint((int)v)) ? (char)v : ' ';
            ft_u8printf_ln(tbl, "%zu|%ld|%lX|%g|%c|%p",
                           n, (long)v, (long)v, r, c, (void *)(intptr_t)v);
        }
        ff_printf(ff, "\n%s", (const char *)ft_to_u8string(tbl));
        ft_destroy_table(tbl);
    }
    _FF_NEXT();

/**
 * `.(` — print an inline `( … )` string. Distinguishes direct-entry
 * from ff_eval (immediate mode: anticipate a string token) vs runtime
 * from compiled heap (string follows inline).
 */
case FF_OP_DOT_PAREN:
    if (ip >= &exec_scratch[0] && ip <= &exec_scratch[2])
    {
        ff->state |= FF_STATE_STRLIT_ANTIC;
    }
    else
    {
        _FF_SYNC();
        ff_printf(ff, "%s", (const char *)(ip + 1));
        ip += *ip;
    }
    _FF_NEXT();

/** ( -- )  `."` — compile a print of the next string literal. */
case FF_OP_DOTQUOTE:
    _FF_COMPILING;
    ff->state |= FF_STATE_STRLIT_ANTIC;
    ff_heap_compile_op(&ff_dict_top(&ff->dict)->heap, FF_OP_DOT_PAREN);
    _FF_NEXT();
