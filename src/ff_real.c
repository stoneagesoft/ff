/**
 * @file ff_real.c
 * @brief Reals as text, whatever the C locale (see ff_real_p.h).
 */

#include "ff_real_p.h"

#include <ff_config_p.h>

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * @param s NUL-terminated text.
 * @return true if @p s has the form of a real literal (see
 *         ff_real_parse()).
 */
static bool ff_real_syntax(const char *s)
{
    const char *p = s;
    if (*p == '-')
        ++p;
    size_t digits = 0;
    for (; isdigit((unsigned char)*p); ++p)
        ++digits;
    if (*p == '.')
        for (++p; isdigit((unsigned char)*p); ++p)
            ++digits;
    if (digits == 0)
        return false;
    if (*p == 'e' || *p == 'E')
    {
        ++p;
        if (*p == '+' || *p == '-')
            ++p;
        if (!isdigit((unsigned char)*p))
            return false;
        while (isdigit((unsigned char)*p))
            ++p;
    }
    return *p == '\0';
}

/** @copydoc ff_real_parse */
bool ff_real_parse(const char *s, ff_real_t *out)
{
    if (!ff_real_syntax(s))
        return false;

    /* strtod() reads the locale's decimal point: hand it that one. */
    const char *dp = localeconv()->decimal_point;
    size_t dp_len = strlen(dp);
    char buf[2 * FF_TOKEN_SIZE];
    size_t n = 0;
    for (const char *p = s; *p; ++p)
    {
        const char *piece = *p == '.' ? dp : p;
        size_t len = *p == '.' ? dp_len : 1;
        if (n + len >= sizeof(buf))
            return false;
        memcpy(buf + n, piece, len);
        n += len;
    }
    buf[n] = '\0';

    char *end;
    errno = 0;
#ifdef FF_32BIT
    ff_real_t v = strtof(buf, &end);
    bool overflow = errno == ERANGE && (v == HUGE_VALF || v == -HUGE_VALF);
#else
    ff_real_t v = strtod(buf, &end);
    bool overflow = errno == ERANGE && (v == HUGE_VAL || v == -HUGE_VAL);
#endif
    /* Underflow sets ERANGE too, for a value that is merely tiny. */
    if (*end != '\0' || overflow)
        return false;
    *out = v;
    return true;
}

/** @copydoc ff_real_format */
int ff_real_format(char *buf, size_t size, const char *fmt, double r)
{
    int n = snprintf(buf, size, fmt, r);
    const char *dp = localeconv()->decimal_point;
    if (n < 0 || size == 0 || strcmp(dp, ".") == 0)
        return n;

    size_t dp_len = strlen(dp);
    char *at = dp_len ? strstr(buf, dp) : NULL;
    if (at)
    {
        *at = '.';
        memmove(at + 1, at + dp_len, strlen(at + dp_len) + 1);
        n -= (int)(dp_len - 1);
    }
    return n;
}
