/**
 * @file ff_md.c
 * @brief Markdown → terminal rendering. Wraps md4c's SAX-style
 *        parser with a callback set that emits plain UTF-8 (or
 *        ANSI-styled UTF-8) into a snprintf-shaped buffer.
 */

#include "ff_md.h"

#include <fort/fort.h>
#include <md4c/md4c.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#define FF_MD_MAX_NESTING  16
#define FF_MD_WBUF_SIZE    256
#define FF_MD_CELL_SIZE    1024


/* ---------------------------------------------------------------------
 * Render context. Threaded through every md4c callback as `userdata`.
 *
 * Output is double-buffered: words are accumulated in `wbuf` until a
 * whitespace boundary, then flushed via `flush_word` so word-wrap
 * can inspect the word length before deciding whether to break.
 * ANSI escape sequences and structural punctuation bypass `wbuf`
 * and emit directly through `emit_raw`.
 * ------------------------------------------------------------------ */
typedef struct
{
    char  *buf;          /* Destination buffer (may be NULL). */
    size_t size;         /* Capacity. */
    size_t pos;          /* Bytes that *would* have been written (snprintf return). */
    int    width;        /* Wrap column; 0 disables wrap. */
    int    col;          /* Current visible column (0-based). */
    int    ansi;         /* Emit ANSI escape codes? */
    int    in_code_block;/* Inside a fenced/indented code block. */
    int    suppress_wrap;/* Inside an inline-code span (preserve whitespace). */

    /* List nesting: each level is unordered ('u') or ordered ('o', counter). */
    struct
    {
        char kind;
        int  counter;
    } list_stack[FF_MD_MAX_NESTING];
    int  list_depth;
    int  blockquote_depth;

    /* Pending-word buffer. */
    char   wbuf[FF_MD_WBUF_SIZE];
    size_t wlen;
    int    wcol;          /* Visible width of the pending word. */

    /* Table state. While inside MD_BLOCK_TABLE, `table` holds a fort
       table being populated; while inside a cell (MD_BLOCK_TH/TD),
       `cell_buf` redirects emit_raw so the cell text is captured
       instead of going to the main output. After the table is
       complete, fort's rendered string is emitted to the main buf. */
    ft_table_t *table;
    int         in_th;          /* current cell is a header cell */
    char        cell_buf[FF_MD_CELL_SIZE];
    size_t      cell_pos;
    int         saved_col;      /* col snapshot to restore after the table */
} ff_md_ctx_t;


/* ---------------------------------------------------------------------
 * Snprintf-style emission. `pos` always tracks "what would have been
 * written" so callers can pre-size with size=0; bytes only land in
 * `buf` when there's room (saving 1 for the NUL).
 * ------------------------------------------------------------------ */
static void emit_raw(ff_md_ctx_t *c, const char *s, size_t n)
{
    /* Inside a table cell: capture into cell_buf for fort, do NOT
       advance the main snprintf cursor. Fort's rendered output will
       be emitted (and counted) when the table finishes. */
    if (c->table && c->cell_pos != (size_t)-1)
    {
        if (c->cell_pos + n + 1 < sizeof(c->cell_buf))
        {
            memcpy(c->cell_buf + c->cell_pos, s, n);
            c->cell_pos += n;
        }
        else if (c->cell_pos + 1 < sizeof(c->cell_buf))
        {
            size_t room = sizeof(c->cell_buf) - c->cell_pos - 1;
            memcpy(c->cell_buf + c->cell_pos, s, room);
            c->cell_pos = sizeof(c->cell_buf) - 1;
        }
        return;
    }

    if (c->buf && c->pos + 1 < c->size)
    {
        size_t room = c->size - c->pos - 1;
        size_t take = n < room ? n : room;
        memcpy(c->buf + c->pos, s, take);
    }
    c->pos += n;
}

/* Visible width: skips ANSI escape sequences and counts UTF-8
   codepoints (one per non-continuation byte). */
static int visible_width(const char *s, size_t n)
{
    int w = 0;
    for (size_t i = 0; i < n; )
    {
        unsigned char b = (unsigned char)s[i];
        if (b == 0x1b)
        {
            ++i;
            while (i < n && (s[i] < 0x40 || s[i] > 0x7e))
                ++i;
            if (i < n) ++i;
        }
        else if ((b & 0xc0) == 0x80)
        {
            ++i;
        }
        else
        {
            ++w;
            ++i;
        }
    }
    return w;
}

/* Emit + update visible column. \n resets col; ANSI escapes are
   skipped; UTF-8 continuation bytes don't count. */
static void emit(ff_md_ctx_t *c, const char *s, size_t n)
{
    emit_raw(c, s, n);
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\n')
        {
            c->col = 0;
        }
        else if (ch == 0x1b)
        {
            ++i;
            while (i < n && (s[i] < 0x40 || s[i] > 0x7e))
                ++i;
            if (i >= n) break;
        }
        else if ((ch & 0xc0) != 0x80)
        {
            c->col++;
        }
    }
}

static void emit_str(ff_md_ctx_t *c, const char *s)
{
    emit(c, s, strlen(s));
}

/* Indent at start of a line. Reflects list / blockquote / code-block
   depth. Caller is responsible for ensuring we're at column 0. */
static void emit_indent(ff_md_ctx_t *c)
{
    int n = c->list_depth * 2 + c->blockquote_depth * 2;
    if (c->in_code_block) n += 4;
    for (int i = 0; i < n; ++i)
        emit(c, " ", 1);
}

/* Newline + indent if we're not already at column 0. */
static void start_line(ff_md_ctx_t *c)
{
    if (c->col > 0)
        emit(c, "\n", 1);
    emit_indent(c);
}

/* Flush the pending word: if appending it would exceed `width`, wrap
   first. Wrap is suppressed while we're capturing a table cell —
   fort handles its own column layout. */
static void flush_word(ff_md_ctx_t *c)
{
    if (c->wlen == 0)
        return;

    int in_cell = (c->cell_pos != (size_t)-1);

    if (!in_cell && c->width > 0 && c->col > 0
        && c->col + c->wcol > c->width)
    {
        emit(c, "\n", 1);
        emit_indent(c);
    }

    emit_raw(c, c->wbuf, c->wlen);
    c->col += c->wcol;
    c->wlen = 0;
    c->wcol = 0;
}

static void word_append(ff_md_ctx_t *c, const char *s, size_t n)
{
    if (c->wlen + n > sizeof(c->wbuf))
    {
        flush_word(c);
        if (n > sizeof(c->wbuf))
        {
            /* Word longer than the wrap buffer: emit raw. */
            emit_raw(c, s, n);
            c->col += visible_width(s, n);
            return;
        }
    }
    memcpy(c->wbuf + c->wlen, s, n);
    c->wlen += n;
    c->wcol += visible_width(s, n);
}

/* Style escape: flushes any pending word so the boundary lands at
   the right place, then emits the raw escape (no col / wbuf
   accounting since escapes are zero-width). */
static void emit_style(ff_md_ctx_t *c, const char *seq)
{
    if (!c->ansi)
        return;
    flush_word(c);
    emit_str(c, seq);
}

/* Word-wrap-aware text emit. Splits @p s on whitespace; codeblock /
   inline-code suppress_wrap mode preserves the input verbatim. */
static void emit_text(ff_md_ctx_t *c, const char *s, size_t n)
{
    if (c->in_code_block || c->suppress_wrap)
    {
        flush_word(c);
        for (size_t i = 0; i < n; ++i)
        {
            if (s[i] == '\n')
            {
                emit(c, "\n", 1);
                emit_indent(c);
            }
            else
            {
                emit(c, &s[i], 1);
            }
        }
        return;
    }

    size_t i = 0;
    while (i < n)
    {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')
        {
            flush_word(c);
            if (s[i] == '\n')
            {
                emit(c, "\n", 1);
                emit_indent(c);
            }
            else if (c->col > 0)
            {
                emit(c, " ", 1);
            }
            ++i;
        }
        else
        {
            size_t start = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != '\n')
                ++i;
            word_append(c, s + start, i - start);
        }
    }
}


/* ---------------------------------------------------------------------
 * md4c callbacks.
 * ------------------------------------------------------------------ */

static int enter_block(MD_BLOCKTYPE type, void *detail, void *ud)
{
    ff_md_ctx_t *c = (ff_md_ctx_t *)ud;
    switch (type)
    {
        case MD_BLOCK_DOC:
            break;

        case MD_BLOCK_H:
        {
            MD_BLOCK_H_DETAIL *d = (MD_BLOCK_H_DETAIL *)detail;
            start_line(c);
            if (c->ansi)
                emit_style(c, d->level == 1 ? "\033[1;4m" : "\033[1m");
            break;
        }

        case MD_BLOCK_P:
            start_line(c);
            break;

        case MD_BLOCK_QUOTE:
            ++c->blockquote_depth;
            break;

        case MD_BLOCK_UL:
            if (c->list_depth < FF_MD_MAX_NESTING - 1)
            {
                ++c->list_depth;
                c->list_stack[c->list_depth - 1].kind = 'u';
                c->list_stack[c->list_depth - 1].counter = 0;
            }
            break;

        case MD_BLOCK_OL:
        {
            MD_BLOCK_OL_DETAIL *d = (MD_BLOCK_OL_DETAIL *)detail;
            if (c->list_depth < FF_MD_MAX_NESTING - 1)
            {
                ++c->list_depth;
                c->list_stack[c->list_depth - 1].kind = 'o';
                c->list_stack[c->list_depth - 1].counter = (int)d->start - 1;
            }
            break;
        }

        case MD_BLOCK_LI:
            start_line(c);
            if (c->list_depth > 0)
            {
                if (c->list_stack[c->list_depth - 1].kind == 'u')
                {
                    emit(c, "* ", 2);
                }
                else
                {
                    char tmp[16];
                    int n = snprintf(tmp, sizeof(tmp), "%d. ",
                                     ++c->list_stack[c->list_depth - 1].counter);
                    emit(c, tmp, (size_t)n);
                }
            }
            break;

        case MD_BLOCK_HR:
        {
            start_line(c);
            int hrlen = c->width > 4 ? c->width - c->col - 1 : 60;
            for (int i = 0; i < hrlen; ++i)
                emit(c, "-", 1);
            emit(c, "\n", 1);
            break;
        }

        case MD_BLOCK_CODE:
            start_line(c);
            c->in_code_block = 1;
            if (c->ansi)
                emit_str(c, "\033[36m");
            emit_indent(c);
            break;

        case MD_BLOCK_TABLE:
            /* Allocate a fort table; cells will accumulate into
               cell_buf via the redirect in emit_raw, then be
               written to the table at TH/TD leave-block. */
            start_line(c);
            c->table = ft_create_table();
            if (c->table)
            {
                ft_set_border_style(c->table, FT_SOLID_ROUND_STYLE);
            }
            c->saved_col = c->col;
            c->cell_pos = (size_t)-1;
            break;

        case MD_BLOCK_THEAD:
            if (c->table)
                ft_set_cell_prop(c->table, 0, FT_ANY_COLUMN,
                                 FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
            break;

        case MD_BLOCK_TBODY:
            break;

        case MD_BLOCK_TR:
            /* New row: nothing to do until cells fill in. */
            break;

        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            /* Begin capturing cell content. Reset the per-cell
               buffer; nested spans (bold/italic/code) will write
               their ANSI codes plus text into cell_buf. col goes
               to 0 so emit_text's word-spacing logic starts fresh
               inside the cell — the wrap branch is suppressed by
               the cell-pos check inside flush_word. */
            c->cell_pos = 0;
            c->col = 0;
            c->in_th = (type == MD_BLOCK_TH);
            break;

        default:
            break;
    }
    return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *ud)
{
    (void)detail;
    ff_md_ctx_t *c = (ff_md_ctx_t *)ud;
    switch (type)
    {
        case MD_BLOCK_H:
            flush_word(c);
            if (c->ansi)
                emit_style(c, "\033[0m");
            emit(c, "\n\n", 2);
            break;

        case MD_BLOCK_P:
            flush_word(c);
            emit(c, "\n\n", 2);
            break;

        case MD_BLOCK_QUOTE:
            if (c->blockquote_depth > 0)
                --c->blockquote_depth;
            break;

        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (c->list_depth > 0)
                --c->list_depth;
            if (c->list_depth == 0)
                emit(c, "\n\n", 2);
            break;

        case MD_BLOCK_LI:
            flush_word(c);
            break;

        case MD_BLOCK_CODE:
            flush_word(c);
            if (c->ansi)
                emit_str(c, "\033[0m");
            emit(c, "\n", 1);
            c->in_code_block = 0;
            break;

        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            /* Flush any pending word (still capturing), null-terminate
               the cell buffer, and write it to fort. Then close the
               capture so any inter-cell whitespace doesn't leak in. */
            flush_word(c);
            if (c->cell_pos < sizeof(c->cell_buf))
                c->cell_buf[c->cell_pos] = '\0';
            else
                c->cell_buf[sizeof(c->cell_buf) - 1] = '\0';
            if (c->table)
                ft_u8write(c->table, c->cell_buf);
            c->cell_pos = (size_t)-1;
            break;

        case MD_BLOCK_TR:
            if (c->table)
                ft_ln(c->table);
            break;

        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;

        case MD_BLOCK_TABLE:
            /* Render the fort table to a string and emit through the
               normal path so column-tracking and snprintf-pos stay
               consistent. */
            if (c->table)
            {
                const char *s = (const char *)ft_to_u8string(c->table);
                if (s)
                    emit_str(c, s);
                ft_destroy_table(c->table);
                c->table = NULL;
            }
            c->col = c->saved_col;
            emit(c, "\n", 1);
            break;

        default:
            break;
    }
    return 0;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *ud)
{
    (void)detail;
    ff_md_ctx_t *c = (ff_md_ctx_t *)ud;
    switch (type)
    {
        case MD_SPAN_EM:
            emit_style(c, "\033[3m");
            break;
        case MD_SPAN_STRONG:
            emit_style(c, "\033[1m");
            break;
        case MD_SPAN_CODE:
            emit_style(c, "\033[36m");
            c->suppress_wrap++;
            break;
        case MD_SPAN_DEL:
            emit_style(c, "\033[9m");
            break;
        case MD_SPAN_A:
            emit_style(c, "\033[4m");
            break;
        default:
            break;
    }
    return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *ud)
{
    ff_md_ctx_t *c = (ff_md_ctx_t *)ud;
    switch (type)
    {
        case MD_SPAN_EM:
            emit_style(c, "\033[23m");
            break;

        case MD_SPAN_STRONG:
            emit_style(c, "\033[22m");
            break;

        case MD_SPAN_CODE:
            emit_style(c, "\033[39m");
            if (c->suppress_wrap > 0)
                c->suppress_wrap--;
            break;

        case MD_SPAN_DEL:
            emit_style(c, "\033[29m");
            break;

        case MD_SPAN_A:
        {
            emit_style(c, "\033[24m");
            MD_SPAN_A_DETAIL *d = (MD_SPAN_A_DETAIL *)detail;
            if (d && d->href.size > 0)
            {
                flush_word(c);
                emit(c, " (", 2);
                if (c->ansi)
                    emit_str(c, "\033[2m");
                emit(c, d->href.text, d->href.size);
                if (c->ansi)
                    emit_str(c, "\033[22m");
                emit(c, ")", 1);
            }
            break;
        }

        default:
            break;
    }
    return 0;
}

static int text_cb(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ud)
{
    ff_md_ctx_t *c = (ff_md_ctx_t *)ud;
    switch (type)
    {
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY:
            emit_text(c, text, size);
            break;

        case MD_TEXT_CODE:
            emit_text(c, text, size);
            break;

        case MD_TEXT_BR:
            flush_word(c);
            emit(c, "\n", 1);
            emit_indent(c);
            break;

        case MD_TEXT_SOFTBR:
            flush_word(c);
            if (c->col > 0)
                emit(c, " ", 1);
            break;

        case MD_TEXT_NULLCHAR:
        case MD_TEXT_HTML:
        case MD_TEXT_LATEXMATH:
        default:
            emit_text(c, text, size);
            break;
    }
    return 0;
}


/* ---------------------------------------------------------------------
 * Public entry points.
 * ------------------------------------------------------------------ */

static int do_render(char *buf, size_t size, const char *md, int width, int ansi)
{
    if (buf && size > 0)
        buf[0] = '\0';

    if (!md)
        return 0;

    ff_md_ctx_t c = {
        .buf              = buf,
        .size             = size,
        .pos              = 0,
        .width            = width,
        .col              = 0,
        .ansi             = ansi,
        .in_code_block    = 0,
        .suppress_wrap    = 0,
        .list_depth       = 0,
        .blockquote_depth = 0,
        .wlen             = 0,
        .wcol             = 0,
        .table            = NULL,
        .cell_pos         = (size_t)-1,
        .saved_col        = 0,
        .in_th            = 0,
    };

    MD_PARSER parser = {
        .abi_version = 0,
        .flags       = MD_FLAG_COLLAPSEWHITESPACE
                     | MD_FLAG_TABLES
                     | MD_FLAG_STRIKETHROUGH
                     | MD_FLAG_PERMISSIVEURLAUTOLINKS
                     | MD_FLAG_PERMISSIVEEMAILAUTOLINKS,
        .enter_block = enter_block,
        .leave_block = leave_block,
        .enter_span  = enter_span,
        .leave_span  = leave_span,
        .text        = text_cb,
        .debug_log   = NULL,
        .syntax      = NULL,
    };

    md_parse(md, (MD_SIZE)strlen(md), &parser, &c);
    flush_word(&c);

    if (buf && size > 0)
        buf[c.pos < size ? c.pos : size - 1] = '\0';

    return (int)c.pos;
}

int ff_md_snprintf(char *buf, size_t size, const char *md, int width)
{
    return do_render(buf, size, md, width, 0);
}

int ff_md_vt_snprintf(char *buf, size_t size, const char *md, int width)
{
    return do_render(buf, size, md, width, 1);
}
