/**
 * @file ff_tokenizer.c
 * @brief Whitespace-delimited Forth lexer with UTF-8 support, line-
 *        spanning `(` comments, and string-literal escape decoding.
 */

#include "ff_tokenizer_p.h"

#include <utf8/utf8.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>


/**
 * Read exactly @p nchars hex digits from the input.
 *
 * @param src    Source text.
 * @param pos    In/out cursor; advanced past the digits on success.
 * @param nchars Expected digit count.
 * @return Decoded value, or -1 if any character is not a hex digit.
 */
static int ff_tok_read_hex(const char *src, int *pos, int nchars)
{
    int v = 0;
    for (int i = 0; i < nchars; ++i)
    {
        if (ff_tok_eof(src, *pos))
            return -1;
        int c = ff_tok_get(src, pos);
        int d;
        if (c >= '0' && c <= '9')       d = c - '0';
        else if (c >= 'a' && c <= 'f')  d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F')  d = 10 + c - 'A';
        else                            return -1;
        v = (v << 4) | d;
    }
    return v;
}

/**
 * Encode a Unicode codepoint as UTF-8 and append it to the tokenizer's
 * token buffer (silently truncates on overflow).
 * @param t  Tokenizer.
 * @param cp Codepoint.
 */
static void ff_tok_append_codepoint(ff_tokenizer_t *t, utf8_int32_t cp)
{
    size_t avail = sizeof(t->token) - 1 - t->token_len;
    utf8_int8_t *start = (utf8_int8_t *)(t->token + t->token_len);
    utf8_int8_t *end   = utf8catcodepoint(start, cp, avail);
    if (end)
        t->token_len += (size_t)(end - start);
}

/**
 * Append a single raw byte to the token buffer, truncating at capacity.
 * @param t Tokenizer.
 * @param b Byte to append.
 */
static void ff_tok_append_byte(ff_tokenizer_t *t, char b)
{
    if (t->token_len < sizeof(t->token) - 1)
        t->token[t->token_len++] = b;
}


// Public

/** @copydoc ff_tokenizer_init */
void ff_tokenizer_init(ff_tokenizer_t *t)
{
    memset(t, 0, sizeof(*t));
}

/** @copydoc ff_tokenizer_destroy */
void ff_tokenizer_destroy(ff_tokenizer_t *t)
{
    memset(t, 0, sizeof(*t));
}

/** @copydoc ff_tokenizer_next */
ff_token_t ff_tokenizer_next(ff_tokenizer_t *t, const char *src, int *pos)
{
    t->token_len = 0;

    for (;;)
    {
        /* Handle pending block comment. */
        if ((t->state & FF_TOK_STATE_COMMENT))
        {
            while (!ff_tok_eof(src, *pos)
                        && ff_tok_get(src, pos) != ')')
            {}
            if (ff_tok_eof(src, *pos))
                return FF_TOKEN_NULL;
            t->state &= ~FF_TOK_STATE_COMMENT;
        }

        /* Skip whitespace. */
        int c;
        do
        {
            if (ff_tok_eof(src, *pos))
                return FF_TOKEN_NULL;
            c = ff_tok_get(src, pos);
        }
        while (isspace((unsigned char)c));

        t->token_len = 0;

        if (c == '"')
        {
            /* String literal. */
            bool rstring = false;
            for (;;)
            {
                if (ff_tok_eof(src, *pos))
                {
                    rstring = true;
                    break;
                }
                c = ff_tok_get(src, pos);
                if (c == '"')
                    break;
                if (c == '\\')
                {
                    if (ff_tok_eof(src, *pos))
                    {
                        rstring = true;
                        break;
                    }
                    c = ff_tok_get(src, pos);
                    switch (c)
                    {
                        case 'b': c = '\b'; break;
                        case 'f': c = '\f'; break;
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case '\\': c = '\\'; break;
                        case '"': c = '"';  break;

                        case 'x':
                        {
                            /* \xHH --- raw byte (two hex digits). */
                            int b = ff_tok_read_hex(src, pos, 2);
                            if (b < 0) b = 0xFF;
                            ff_tok_append_byte(t, (char)b);
                            continue;
                        }

                        case 'u':
                        {
                            /* \uXXXX --- BMP code point (four hex digits). */
                            int cp = ff_tok_read_hex(src, pos, 4);
                            if (cp < 0) cp = 0xFFFD;
                            ff_tok_append_codepoint(t, (utf8_int32_t)cp);
                            continue;
                        }

                        case 'U':
                        {
                            /* \UXXXXXXXX --- any code point (eight hex digits). */
                            int cp = ff_tok_read_hex(src, pos, 8);
                            if (cp < 0) cp = 0xFFFD;
                            ff_tok_append_codepoint(t, (utf8_int32_t)cp);
                            continue;
                        }

                        default:
                            /* Unknown escape: preserve the backslash. */
                            if (t->token_len < sizeof(t->token) - 1)
                                t->token[t->token_len++] = '\\';
                            break;
                    }
                }
                if (t->token_len < sizeof(t->token) - 1)
                    t->token[t->token_len++] = (char)c;
            }
            t->token[t->token_len] = '\0';
            if (rstring)
                return FF_TOKEN_NULL;
            return FF_TOKEN_STRING;
        }

        /* Raw token (non-string). */
        do
        {
            if (t->token_len < (int)sizeof(t->token) - 1)
                t->token[t->token_len++] = (char)c;
            if (ff_tok_eof(src, *pos))
                break;
            c = ff_tok_get(src, pos);
        }
        while (!isspace((unsigned char)c));
        t->token[t->token_len] = '\0';

        if (t->token_len == 0)
            return FF_TOKEN_NULL;

        /* Line comment: backslash. */
        if (t->token_len == 1
                && t->token[0] == '\\')
        {
            while (!ff_tok_eof(src, *pos) && src[*pos] != '\n')
                (*pos)++;
            continue;
        }

        /* Block comment: open paren. */
        if (t->token_len == 1
                && t->token[0] == '(')
        {
            while (!ff_tok_eof(src, *pos)
                        && ff_tok_get(src, pos) != ')')
            {}
            if (ff_tok_eof(src, *pos))
                t->state |= FF_TOK_STATE_COMMENT;
            continue;
        }

        /* Try to parse as number. */
        if (isdigit((unsigned char)t->token[0])
                || t->token[0] == '-')
        {
            char *end;
#ifdef FF_32BIT
            t->integer_val = strtol(t->token, &end, 0);
#else
            t->integer_val = strtoll(t->token, &end, 0);
#endif
            if (*end == '\0')
                return FF_TOKEN_INTEGER;

#ifdef FF_32BIT
            t->real_val = strtof(t->token, &end);
#else
            t->real_val = strtod(t->token, &end);
#endif
            if (*end == '\0')
                return FF_TOKEN_REAL;
        }

        return FF_TOKEN_WORD;
    }
}


// Private

/**
 * Read one byte from @p src at `*pos` and advance the cursor.
 * @param src Source text.
 * @param pos In/out cursor.
 * @return The byte (as unsigned int), or -1 at end-of-input.
 */
int ff_tok_get(const char *src, int *pos)
{
    unsigned char c = src[*pos];
    if (c == '\0')
        return -1;
    (*pos)++;
    return c;
}

/**
 * @param src Source text.
 * @param pos Cursor position.
 * @return true iff `src[pos]` is the terminating NUL.
 */
bool ff_tok_eof(const char *src, int pos)
{
    return src[pos] == '\0';
}
