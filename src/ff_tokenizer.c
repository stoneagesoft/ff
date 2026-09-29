/**
 * @file ff_tokenizer.c
 * @brief Whitespace-delimited Forth lexer with UTF-8 support, line-
 *        spanning `(` comments, and string-literal escape decoding.
 */

#include "ff_tokenizer_p.h"

#include "ff_real_p.h"

#include <utf8/utf8.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>


static int ff_tok_get(const char *src, int *pos);
static bool ff_tok_eof(const char *src, int pos);


/**
 * Read exactly @p nchars hex digits from the input.
 *
 * Only hex digits are consumed. On a short or non-hex sequence the cursor
 * stops *at* the offending character, which is then scanned as usual —
 * otherwise `"\x"` would swallow its own closing quote and run the string
 * on into whatever follows.
 *
 * @param src    Source text.
 * @param pos    In/out cursor; advanced past the digits read.
 * @param nchars Expected digit count (at most 8).
 * @param out    Decoded value on success.
 * @return false if fewer than @p nchars hex digits follow.
 */
static bool ff_tok_read_hex(const char *src, int *pos, int nchars, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < nchars; ++i)
    {
        int c = (unsigned char)src[*pos];
        uint32_t d;
        if (c >= '0' && c <= '9')       d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')  d = (uint32_t)(10 + c - 'a');
        else if (c >= 'A' && c <= 'F')  d = (uint32_t)(10 + c - 'A');
        else                            return false;
        ++*pos;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

/**
 * Encode a Unicode codepoint as UTF-8 and append it to the tokenizer's
 * token buffer, flagging truncation if it doesn't fit.
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
    else
        t->truncated = true;
}

/**
 * Append a single raw byte to the token buffer, flagging truncation at
 * capacity.
 * @param t Tokenizer.
 * @param b Byte to append.
 */
static void ff_tok_append_byte(ff_tokenizer_t *t, char b)
{
    if (t->token_len < sizeof(t->token) - 1)
        t->token[t->token_len++] = b;
    else
        t->truncated = true;
}

/**
 * Parse a `0x…` / `-0x…` literal (@p digits points past the `0x`).
 *
 * Hex is read as a bit pattern, the way an assembler reads it, so the
 * whole unsigned cell range is accepted: `0xFFFFFFFFFFFFFFFF` is -1. A
 * negated literal must still fit a signed cell. Anything else is not a
 * number; it is deliberately never retried as a real, because strtod
 * accepts hex floats and would push an out-of-range `0x…` as a double.
 *
 * @param digits Hex digits after the prefix.
 * @param neg    A leading '-' preceded the prefix.
 * @param out    Parsed cell on success.
 * @return true if the whole of @p digits is a valid hex cell.
 */
static bool ff_tok_parse_hex(const char *digits, bool neg, ff_int_t *out)
{
    /* strtoull would also accept leading blanks and a sign here. */
    if (!isxdigit((unsigned char)digits[0]))
        return false;

    char *end;
    errno = 0;
    unsigned long long u = strtoull(digits, &end, 16);
    if (*end != '\0' || errno == ERANGE)
        return false;
#ifdef FF_32BIT
    if (u > UINT32_MAX)
        return false;
#endif
    if (neg && u > (unsigned long long)FF_INT_MAX + 1)
        return false;

    *out = (ff_int_t)(neg ? (ff_uint_t)0 - (ff_uint_t)u : (ff_uint_t)u);
    return true;
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
    t->truncated = false;
    t->bad_escape = false;

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
        t->pos = *pos - 1;      /* where this token starts, for error reports */

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

                        /* A malformed numeric escape flags the literal
                           for the evaluator to reject, rather than being
                           papered over with a substitute character; the
                           scan continues so the lexer stays in step with
                           the closing quote. */
                        case 'x':
                        {
                            /* \xHH --- raw byte (two hex digits). */
                            uint32_t b;
                            if (ff_tok_read_hex(src, pos, 2, &b))
                                ff_tok_append_byte(t, (char)b);
                            else
                                t->bad_escape = true;
                            continue;
                        }

                        case 'u':
                        case 'U':
                        {
                            /* \uXXXX --- BMP code point (four hex digits);
                               \UXXXXXXXX --- any code point (eight). A
                               surrogate or a value past U+10FFFF has no
                               UTF-8 encoding. */
                            uint32_t cp;
                            if (ff_tok_read_hex(src, pos, c == 'u' ? 4 : 8, &cp)
                                    && cp <= 0x10FFFF
                                    && (cp < 0xD800 || cp > 0xDFFF))
                                ff_tok_append_codepoint(t, (utf8_int32_t)cp);
                            else
                                t->bad_escape = true;
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
                else
                    t->truncated = true;
            }
            t->token[t->token_len] = '\0';
            if (rstring)
            {
                /* Unterminated string: flag it so the evaluator can raise
                   FF_ERR_RUN_STRING rather than silently treating this as
                   clean end-of-input. */
                t->state |= FF_TOK_STATE_STRING;
                return FF_TOKEN_NULL;
            }
            return FF_TOKEN_STRING;
        }

        /* Raw token (non-string). */
        do
        {
            if (t->token_len < (int)sizeof(t->token) - 1)
                t->token[t->token_len++] = (char)c;
            else
                t->truncated = true;
            if (ff_tok_eof(src, *pos))
                break;
            c = ff_tok_get(src, pos);
        }
        while (!isspace((unsigned char)c));
        t->token[t->token_len] = '\0';

        if (t->token_len == 0)
            return FF_TOKEN_NULL;

        /* Line comment: backslash. The whitespace that ended the token
           has been consumed; if it was the newline, so is the comment —
           skipping on to the next newline commented out the next line
           too. */
        if (t->token_len == 1
                && t->token[0] == '\\')
        {
            if (c != '\n')
                while (!ff_tok_eof(src, *pos) && src[*pos] != '\n')
                    (*pos)++;
            continue;
        }

        /* Block comment: open paren. Suppressed in signature mode, where
           `{` needs `( a b -- c )` delivered as ordinary tokens rather
           than skipped as a comment. */
        if (t->token_len == 1
                && t->token[0] == '('
                && !(t->state & FF_TOK_STATE_SIG))
        {
            while (!ff_tok_eof(src, *pos)
                        && ff_tok_get(src, pos) != ')')
            {}
            if (ff_tok_eof(src, *pos))
                t->state |= FF_TOK_STATE_COMMENT;
            continue;
        }

        /* Try to parse as number. Integers are base 10 unless prefixed
           with 0x/0X (hex). This deliberately drops C's implicit octal:
           `010` is ten, not eight, and `009` is nine, not a fall-through
           to real. An out-of-range decimal integer (ERANGE) is not
           clamped — it falls through to the real parse, and if that also
           overflows the token is left as an ordinary (likely undefined)
           word rather than silently becoming a wrong value. Hex has its
           own rules; see ff_tok_parse_hex. */
        if (isdigit((unsigned char)t->token[0])
                || t->token[0] == '-')
        {
            char *end;
            bool neg = t->token[0] == '-';
            const char *digits = t->token + (neg ? 1 : 0);
            if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
                return ff_tok_parse_hex(digits + 2, neg, &t->integer_val)
                           ? FF_TOKEN_INTEGER
                           : FF_TOKEN_WORD;

            errno = 0;
#ifdef FF_32BIT
            t->integer_val = strtol(t->token, &end, 10);
#else
            t->integer_val = strtoll(t->token, &end, 10);
#endif
            if (*end == '\0' && errno != ERANGE)
                return FF_TOKEN_INTEGER;

            if (ff_real_parse(t->token, &t->real_val))
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
static int ff_tok_get(const char *src, int *pos)
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
static bool ff_tok_eof(const char *src, int pos)
{
    return src[pos] == '\0';
}
