/*
 * ff --- dict word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <fort/fort.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Dictionary introspection words
 * =================================================================== */

void ff_print_words(ff_t *ff, int filter)
{
    /* filter: 0 = all, 1 = used only, 2 = unused only */
    ff_dict_t *d = &ff->dict;

    int lmax = 0;
    for (size_t i = 0; i < d->count; ++i)
    {
        ff_word_t *w = d->words[i];
        if (filter == 1 && !(w->flags & FF_WORD_USED))
            continue;
        if (filter == 2 && (w->flags & FF_WORD_USED))
            continue;
        int l = (int)strlen(w->name);
        if (lmax < l)
            lmax = l;
    }

    int col = 0;
    for (size_t i = 0; i < d->count; ++i)
    {
        ff_word_t *w = d->words[i];
        if (filter == 1 && !(w->flags & FF_WORD_USED))
            continue;
        if (filter == 2 && (w->flags & FF_WORD_USED))
            continue;
        ff_printf(ff, "%-*s ", lmax, w->name);
        if (++col >= 5)
        {
            ff_printf(ff, "\n");
            col = 0;
        }
    }
    if (col)
        ff_printf(ff, "\n");
}

void ff_print_manual(ff_t *ff, const ff_word_t *w)
{
    if (!w->manual)
    {
        ff_tracef(ff, FF_SEV_WARNING | FF_ERR_NO_MAN,
                  "No manual for '%s'.", w->name);
        return;
    }

    /* The manual's first line is the prototype; the rest is the synopsis. */
    const char *p = w->manual;
    while (*p && (*p == ' ' || *p == '\t'))
        ++p;
    const char *nl = strchr(p, '\n');
    const char *proto_end = nl ? nl : p + strlen(p);

    char proto[256];
    size_t plen = (size_t)(proto_end - p);
    if (plen >= sizeof(proto))
        plen = sizeof(proto) - 1;
    memcpy(proto, p, plen);
    proto[plen] = '\0';

    const char *synopsis = nl ? nl + 1 : "";

    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_ROUND_STYLE);

    /* Header row: Name | Prototype | Immediate. */
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_u8write_ln(table, "Name", "Prototype", "Immediate");

    /* Data row: matching values. */
    ft_set_cell_prop(table, 1, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_u8write_ln(table, w->name, proto,
                  ff_tick(w->flags & FF_WORD_IMMEDIATE));

    /* Horizontal rule between the data row and the synopsis. */
    ft_add_separator(table);

    /* Synopsis: single cell spanning all columns, no header. */
    ft_set_cell_span(table, 2, 0, 3);
    ft_set_cell_prop(table, 2, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_LEFT);
    ft_u8write_ln(table, synopsis);

    ff_printf(ff, "%s", (const char *)ft_to_u8string(table));
    ft_destroy_table(table);
}

void ff_w_man_impl(ff_t *ff)
{
    ff_token_t tok = ff_tokenizer_next(&ff->tokenizer, ff->input, &ff->input_pos);
    if (tok != FF_TOKEN_WORD)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                  "'%s' is not a word.", ff->tokenizer.token);
        return;
    }

    const ff_word_t *w = ff_dict_lookup(&ff->dict, ff->tokenizer.token);
    if (!w)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                  "'%s' undefined.", ff->tokenizer.token);
        return;
    }

    ff_print_manual(ff, w);
}

void ff_dump_heap_cells(ff_t *ff, const ff_heap_t *h);

void ff_w_dump_word_impl(ff_t *ff)
{
    ff_token_t tok = ff_tokenizer_next(&ff->tokenizer, ff->input, &ff->input_pos);
    if (tok != FF_TOKEN_WORD)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                  "'%s' is not a word.", ff->tokenizer.token);
        return;
    }

    const ff_word_t *w = ff_dict_lookup(&ff->dict, ff->tokenizer.token);
    if (!w)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                  "'%s' undefined.", ff->tokenizer.token);
        return;
    }

    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_ROUND_STYLE);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_set_cell_prop(table, 1, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_set_cell_prop(table, 1, 4, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_set_cell_prop(table, 1, 5, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_set_cell_prop(table, 1, 6, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_u8write_ln(table, "Word", "Immediate", "Used", "Native",
                  "Does", "Heap, cells", "Heap, bytes");
    ft_u8printf_ln(table, "%s|%s|%s|%s|%p|%zu|%zu",
                   w->name,
                   ff_tick(w->flags & FF_WORD_IMMEDIATE),
                   ff_tick(w->flags & FF_WORD_USED),
                   ff_tick(ff_word_is_native(w)),
                   (void *)w->does,
                   w->heap.size,
                   w->heap.size * sizeof(ff_int_t));

    ff_printf(ff, "%s", (const char *)ft_to_u8string(table));
    ft_destroy_table(table);

    ff_dump_heap_cells(ff, &w->heap);
}

void ff_dump_heap_cells(ff_t *ff, const ff_heap_t *h)
{
    if (h->size == 0)
        return;

    const char *addr = (const char *)h->data;
    size_t size = h->size * sizeof(ff_int_t);

    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_ROUND_STYLE);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(table, 0, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_u8write_ln(table,
                  "",
                  "0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F ",
                  "0123456789ABCDEF");

    const char *p1 = addr;
    const char *p2 = addr + size - 1;
    for (; p1 <= p2; p1 += 16)
    {
        char addr_buf[20];
        snprintf(addr_buf, sizeof(addr_buf), "%0*" PRIxPTR,
#ifdef FF_32BIT
                 8,
#else
                 16,
#endif
                 (uintptr_t)p1);

        char hex[49];
        memset(hex, 0, sizeof(hex));
        char *dst = hex;
        for (const char *p = p1; p - p1 < 16 && p <= p2; ++p)
        {
            dst += snprintf(dst, sizeof(hex) - (dst - hex), "%02X", (uint8_t)*p);
            if (p - p1 == 7)
                *dst++ = ' ';
            if (p - p1 < 15)
                *dst++ = ' ';
        }

        char ascii[17];
        memset(ascii, 0, sizeof(ascii));
        dst = ascii;
        for (const char *p = p1; p - p1 < 16 && p <= p2; ++p, ++dst)
            *dst = (*p < 0x20 || (unsigned char)*p > 0x7E) ? '.' : *p;

        ft_u8write_ln(table, addr_buf, hex, ascii);
    }

    ff_printf(ff, "%s", (const char *)ft_to_u8string(table));
    ft_destroy_table(table);
}

static const ff_word_t *ff_see_opcode_to_word(ff_dict_t *d, ff_opcode_t opcode)
{
    for (size_t i = 0; i < d->count; ++i)
        if (d->words[i]->opcode == opcode)
            return d->words[i];
    return NULL;
}

/* ---------------------------------------------------------------------
 * Decompiler ('see').
 *
 * The bytecode has no explicit IF/THEN/BEGIN/UNTIL/etc. opcodes — every
 * surface control word reduces to BRANCH/QBRANCH plus offsets, and
 * DO/LOOP reduces to XDO/XLOOP. To reconstruct the source we keep a
 * stack of "open constructs" while walking, and use a pre-pass to
 * locate the targets of backward branches (= BEGIN points).
 *
 * Disambiguation rules:
 *   QBRANCH (forward) — IF, unless we're inside a BEGIN block AND the
 *                       target is past the BEGIN's closing backward
 *                       BRANCH, in which case it's a WHILE.
 *   BRANCH  (forward) — ELSE (when stack top is IF and the offset
 *                       lands past a THEN target).
 *   QBRANCH (backward) — UNTIL (closes a BEGIN).
 *   BRANCH  (backward) — AGAIN, or REPEAT if the BEGIN had a WHILE.
 *   XDO/XQDO  — DO/?DO. Inline operand is offset to after-loop.
 *   XLOOP/PXLOOP — LOOP/+LOOP.
 * ------------------------------------------------------------------- */

typedef enum
{
    SEE_F_IF,    /* end = position of THEN */
    SEE_F_ELSE,  /* end = position of THEN */
    SEE_F_BEGIN, /* begin_at = BEGIN position; has_while tracked */
    SEE_F_DO     /* end = position after LOOP+offset */
} see_kind_t;

typedef struct
{
    see_kind_t kind;
    size_t     end;
    size_t     begin_at;
    int        has_while;
} see_frame_t;

#define SEE_MAX_DEPTH 64

typedef struct
{
    ff_t *ff;
    int   indent;          /* logical indent for the next emit */
    int   at_line_start;   /* 1 = nothing on current line; need to emit indent first */
} see_printer_t;

/* Emit text on the current line. Lays down the indent on the first
   emit after a newline; otherwise inserts a single space separator. */
static void see_text(see_printer_t *p, const char *fmt, ...)
{
    if (p->at_line_start)
    {
        for (int i = 0; i < p->indent; ++i)
            ff_printf(p->ff, "    ");
        p->at_line_start = 0;
    }
    else
    {
        ff_printf(p->ff, " ");
    }
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ff_printf(p->ff, "%s", buf);
}

/* Move to a new line at the given indent level. Idempotent — calling
   it twice in a row produces just one blank-after-text break, not two. */
static void see_newline(see_printer_t *p, int new_indent)
{
    if (!p->at_line_start)
    {
        ff_printf(p->ff, "\n");
        p->at_line_start = 1;
    }
    p->indent = new_indent;
}

/* Block opener: emit keyword on the current line, then indent the
   body one level deeper. */
static void see_open(see_printer_t *p, const char *kw)
{
    see_text(p, "%s", kw);
    see_newline(p, p->indent + 1);
}

/* Block closer: dedent, break, emit keyword on its own line, then
   break again so trailing code lands on the next line. */
static void see_close(see_printer_t *p, const char *kw)
{
    int outer = p->indent - 1;
    see_newline(p, outer);
    see_text(p, "%s", kw);
    see_newline(p, outer);
}

/* Continuation (else, while): dedent, break, emit, break, indent
   one level deeper for the new sub-body. */
static void see_continue(see_printer_t *p, const char *kw)
{
    int outer = p->indent - 1;
    see_newline(p, outer);
    see_text(p, "%s", kw);
    see_newline(p, outer + 1);
}

/* Length of an opcode's encoded form (opcode cell + any inline operand
   cells). Returns 1 unless the opcode carries operands. */
static size_t see_opcode_len(const ff_int_t *cells, size_t pos, size_t end)
{
    if (pos >= end)
        return 1;
    ff_opcode_t op = (ff_opcode_t)cells[pos];
    switch (op)
    {
        case FF_OP_LIT:
        case FF_OP_LITADD:
        case FF_OP_LITSUB:
        case FF_OP_FLIT:
        case FF_OP_BRANCH:
        case FF_OP_QBRANCH:
        case FF_OP_XDO:
        case FF_OP_XQDO:
        case FF_OP_XLOOP:
        case FF_OP_PXLOOP:
        case FF_OP_NEST:
        case FF_OP_TNEST:
        case FF_OP_CALL:
        case FF_OP_DOES_RUNTIME:
        case FF_OP_CREATE_RUNTIME:
        case FF_OP_CONSTANT_RUNTIME:
        case FF_OP_ARRAY_RUNTIME:
        case FF_OP_DEFER_RUNTIME:
            return 2;
        case FF_OP_STRLIT:
            return 1 + (pos + 1 < end ? (size_t)cells[pos + 1] : 0);
        default:
            return 1;
    }
}

/* Pre-pass: mark each cell index that is the target of a backward
   branch. Caller owns the buffer and frees it. */
static void see_mark_begins(const ff_int_t *cells, size_t size, char *is_begin)
{
    for (size_t pos = 0; pos < size; )
    {
        ff_opcode_t op = (ff_opcode_t)cells[pos];
        if ((op == FF_OP_BRANCH || op == FF_OP_QBRANCH) && pos + 1 < size)
        {
            ff_int_t off = cells[pos + 1];
            if (off < 0)
            {
                ssize_t target = (ssize_t)(pos + 1) + off;
                if (target >= 0 && (size_t)target < size)
                    is_begin[target] = 1;
            }
        }
        size_t step = see_opcode_len(cells, pos, size);
        pos += step ? step : 1;
    }
}

/* Decompile a slice of bytecode into pretty Forth source. Used both
   for colon-def bodies and DOES>-clause bodies. The frame stack and
   indent management are local to this function so nested calls (e.g.
   see-ing a DOES> word from within see itself) don't conflict. */
static void see_decompile_body(ff_t *ff, const ff_int_t *cells, size_t size,
                               see_printer_t *pr, int stop_on_exit)
{
    if (size == 0)
        return;

    char *is_begin = (char *)calloc(size, 1);
    if (!is_begin)
        return;
    see_mark_begins(cells, size, is_begin);

    ff_dict_t *d = &ff->dict;
    see_frame_t stack[SEE_MAX_DEPTH];
    int top = 0;

    size_t pos = 0;
    while (pos < size)
    {
        while (top > 0
               && (stack[top - 1].kind == SEE_F_IF
                       || stack[top - 1].kind == SEE_F_ELSE
                       || stack[top - 1].kind == SEE_F_DO)
               && stack[top - 1].end == pos)
        {
            see_close(pr,
                      stack[top - 1].kind == SEE_F_DO ? "loop" : "then");
            top--;
        }

        if (is_begin[pos]
            && (top == 0 || stack[top - 1].kind != SEE_F_BEGIN
                || stack[top - 1].begin_at != pos))
        {
            if (top >= SEE_MAX_DEPTH) goto out;
            stack[top].kind      = SEE_F_BEGIN;
            stack[top].begin_at  = pos;
            stack[top].has_while = 0;
            stack[top].end       = 0;
            top++;
            see_open(pr, "begin");
        }

        ff_opcode_t op = (ff_opcode_t)cells[pos];

        switch (op)
        {
            case FF_OP_LIT:
                see_text(pr, "%ld", (long)cells[pos + 1]);
                pos += 2;
                break;

            case FF_OP_LIT0:    see_text(pr, "0");      pos += 1; break;
            case FF_OP_LIT1:    see_text(pr, "1");      pos += 1; break;
            case FF_OP_LITM1:   see_text(pr, "-1");     pos += 1; break;

            case FF_OP_LITADD:
                see_text(pr, "%ld +", (long)cells[pos + 1]);
                pos += 2;
                break;

            case FF_OP_LITSUB:
                see_text(pr, "%ld -", (long)cells[pos + 1]);
                pos += 2;
                break;

            case FF_OP_FLIT:
            {
                ff_real_t r;
                memcpy(&r, &cells[pos + 1], sizeof(r));
                see_text(pr, "%g", r);
                pos += 2;
                break;
            }

            case FF_OP_STRLIT:
            {
                ff_int_t skip = cells[pos + 1];
                see_text(pr, "\" %s\"", (const char *)&cells[pos + 2]);
                pos += 1 + (size_t)skip;
                break;
            }

            case FF_OP_NEST:
            case FF_OP_TNEST:
            {
                ff_word_t *nw = (ff_word_t *)(intptr_t)cells[pos + 1];
                see_text(pr, "%s", nw->name);
                pos += 2;
                break;
            }

            case FF_OP_CALL:
                see_text(pr, "<native>");
                pos += 2;
                break;

            case FF_OP_EXIT:
                if (stop_on_exit)
                    goto out;
                if (pos + 1 < size)
                    see_text(pr, "exit");
                pos += 1;
                break;

            case FF_OP_QBRANCH:
            {
                ff_int_t off = cells[pos + 1];
                ssize_t target = (ssize_t)(pos + 1) + off;
                if (off > 0)
                {
                    int is_while = 0;
                    for (int i = top - 1; i >= 0; --i)
                    {
                        if (stack[i].kind != SEE_F_BEGIN)
                            continue;
                        if (target >= 2
                            && (size_t)(target - 2) < size
                            && cells[target - 2] == FF_OP_BRANCH
                            && cells[target - 1] < 0)
                        {
                            ssize_t bb_tgt = (ssize_t)(target - 1)
                                                 + cells[target - 1];
                            if ((size_t)bb_tgt == stack[i].begin_at)
                            {
                                is_while = 1;
                                stack[i].has_while = 1;
                            }
                        }
                        break;
                    }
                    if (is_while)
                        see_continue(pr, "while");
                    else
                    {
                        if (top >= SEE_MAX_DEPTH) goto out;
                        stack[top].kind = SEE_F_IF;
                        stack[top].end  = (size_t)target;
                        top++;
                        see_open(pr, "if");
                    }
                }
                else
                {
                    if (top > 0 && stack[top - 1].kind == SEE_F_BEGIN)
                    {
                        see_close(pr, "until");
                        top--;
                    }
                    else
                        see_text(pr, "<until?>");
                }
                pos += 2;
                break;
            }

            case FF_OP_BRANCH:
            {
                ff_int_t off = cells[pos + 1];
                ssize_t target = (ssize_t)(pos + 1) + off;
                if (off > 0)
                {
                    if (top > 0 && stack[top - 1].kind == SEE_F_IF
                        && stack[top - 1].end == pos + 2)
                    {
                        stack[top - 1].kind = SEE_F_ELSE;
                        stack[top - 1].end  = (size_t)target;
                        see_continue(pr, "else");
                    }
                    else
                        see_text(pr, "<else?>");
                }
                else
                {
                    if (top > 0 && stack[top - 1].kind == SEE_F_BEGIN)
                    {
                        see_close(pr,
                                  stack[top - 1].has_while
                                      ? "repeat" : "again");
                        top--;
                    }
                    else
                        see_text(pr, "<again?>");
                }
                pos += 2;
                break;
            }

            case FF_OP_XDO:
            case FF_OP_XQDO:
            {
                ff_int_t off = cells[pos + 1];
                ssize_t end_pos = (ssize_t)(pos + 1) + off;
                if (top >= SEE_MAX_DEPTH) goto out;
                stack[top].kind = SEE_F_DO;
                stack[top].end  = (size_t)end_pos;
                top++;
                see_open(pr, op == FF_OP_XDO ? "do" : "?do");
                pos += 2;
                break;
            }

            case FF_OP_XLOOP:
            case FF_OP_PXLOOP:
                pos += 2;
                break;

            case FF_OP_I_ADD:
                see_text(pr, "i +");
                pos += 1;
                break;

            default:
            {
                const ff_word_t *ow = ff_see_opcode_to_word(d, op);
                if (ow)
                    see_text(pr, "%s", ow->name);
                else
                    see_text(pr, "<%d>", op);
                pos += 1;
                break;
            }
        }
    }

    while (top > 0
           && (stack[top - 1].kind == SEE_F_IF
                   || stack[top - 1].kind == SEE_F_ELSE
                   || stack[top - 1].kind == SEE_F_DO)
           && stack[top - 1].end == size)
    {
        see_close(pr,
                  stack[top - 1].kind == SEE_F_DO ? "loop" : "then");
        top--;
    }

out:
    free(is_begin);
}

/* Find the dictionary word whose heap contains the given pointer.
   Used by `see` to recover the parent of a DOES>-built word. */
static const ff_word_t *see_word_for_pointer(const ff_t *ff, const ff_int_t *p)
{
    for (size_t i = 0; i < ff->dict.count; ++i)
    {
        const ff_word_t *w = ff->dict.words[i];
        if (!w || w->heap.data == NULL) continue;
        const ff_int_t *lo = w->heap.data;
        const ff_int_t *hi = lo + w->heap.capacity;
        if (p >= lo && p < hi) return w;
    }
    return NULL;
}

void ff_w_see_impl(ff_t *ff)
{
    ff_token_t tok = ff_tokenizer_next(&ff->tokenizer, ff->input, &ff->input_pos);
    if (tok != FF_TOKEN_WORD)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                  "'%s' is not a word.", ff->tokenizer.token);
        return;
    }

    const ff_word_t *w = ff_dict_lookup(&ff->dict, ff->tokenizer.token);
    if (!w)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                  "'%s' undefined.", ff->tokenizer.token);
        return;
    }

    if (ff_word_is_native(w))
    {
        ff_printf(ff, "%s is a native word.\n", w->name);
        return;
    }

    /* Words whose body is data (not bytecode) get a one-line summary
       in the form that would have created them. Bytecode-walking only
       makes sense for colon-defs (opcode = FF_OP_NEST / _TNEST). */
    if (w->opcode == FF_OP_CONSTANT_RUNTIME)
    {
        ff_printf(ff, "%ld constant %s\n",
                  (long)(w->heap.size ? w->heap.data[0] : 0), w->name);
        return;
    }
    if (w->opcode == FF_OP_CREATE_RUNTIME)
    {
        ff_printf(ff, "create %s  ( %zu cell%s reserved )\n",
                  w->name, w->heap.size,
                  w->heap.size == 1 ? "" : "s");
        return;
    }
    if (w->opcode == FF_OP_ARRAY_RUNTIME)
    {
        ff_printf(ff, "%zu array %s\n", w->heap.size, w->name);
        return;
    }
    if (w->opcode == FF_OP_DEFER_RUNTIME)
    {
        const ff_word_t *target =
            w->heap.size
                ? (const ff_word_t *)(intptr_t)w->heap.data[0]
                : NULL;
        if (target && ff_word_valid(ff, target))
            ff_printf(ff, "defer %s  ( currently: ' %s is %s )\n",
                      w->name, target->name, w->name);
        else
            ff_printf(ff, "defer %s\n", w->name);
        return;
    }

    /* DOES>-built word: data lives in heap.data; the runtime body
       lives at w->does inside the parent (defining) word's heap. */
    if (w->opcode == FF_OP_DOES_RUNTIME)
    {
        const ff_word_t *parent =
            w->does ? see_word_for_pointer(ff, w->does) : NULL;
        ff_printf(ff, "\\ %s — built by `does>` clause%s%s%s\n",
                  w->name,
                  parent ? " in `" : "",
                  parent ? parent->name : "",
                  parent ? "`" : "");
        ff_printf(ff, "%s  \\ parameter field:", w->name);
        for (size_t i = 0; i < w->heap.size; ++i)
            ff_printf(ff, " %ld", (long)w->heap.data[i]);
        ff_printf(ff, "\n");
        if (w->does && parent)
        {
            see_printer_t pr2 = { .ff = ff, .indent = 0, .at_line_start = 1 };
            size_t off = (size_t)(w->does - parent->heap.data);
            size_t remaining = parent->heap.size > off
                                   ? parent->heap.size - off
                                   : 0;
            see_text(&pr2, "does>");
            see_newline(&pr2, 1);
            see_decompile_body(ff, w->does, remaining, &pr2,
                               /*stop_on_exit=*/1);
            see_newline(&pr2, 0);
            ff_printf(ff, ";\n");
        }
        return;
    }

    if (w->heap.size == 0)
    {
        ff_printf(ff, ": %s ;%s\n", w->name,
                  (w->flags & FF_WORD_IMMEDIATE) ? " immediate" : "");
        return;
    }

    /* Colon-def: walk the bytecode body. */
    see_printer_t pr = { .ff = ff, .indent = 0, .at_line_start = 1 };
    see_text(&pr, ": %s", w->name);
    see_newline(&pr, 1);
    see_decompile_body(ff, w->heap.data, w->heap.size, &pr,
                       /*stop_on_exit=*/0);
    see_newline(&pr, 0);
    ff_printf(ff, ";%s\n",
              (w->flags & FF_WORD_IMMEDIATE) ? " immediate" : "");
}


const ff_word_def_t FF_DICT_WORDS[] =
{
    _FF_W("words", FF_OP_WORDS,
      "( -- )  List words defined\n"
      "Defined words are listed, from the most recently defined to the first defined.\n"
      "\n"
      "See also: **wordsused**, **wordsunused**"),
    _FF_W("wordsused", FF_OP_WORDSUSED,
      "( -- )  List words used\n"
      "The words used by this program are listed on standard output.\n"
      "\n"
      "See also: **words**, **wordsunused**"),
    _FF_W("wordsunused", FF_OP_WORDSUNUSED,
      "( -- )  List words not used\n"
      "The words not used by this program are listed on standard output.\n"
      "\n"
      "See also: **words**, **wordsused**"),
    _FF_W("man", FF_OP_MAN,
      "w ( -- )  Print manual\n"
      "Prints prototype and manual for the dictionary word *w*."),
    _FF_W("dump-word", FF_OP_DUMP_WORD,
      "w ( -- )  Word dump\n"
      "Prints word *w*'s metadata and memory dump of its heap."),
    _FF_W("see", FF_OP_SEE,
      "w ( -- )  De-compile word\n"
      "Translate compiled word *w* back to the source code."),
    FF_WEND
};

