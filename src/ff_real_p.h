/**
 * @file ff_real_p.h
 * @brief Reals as text, whatever the C locale.
 *
 * The C library reads and writes floating-point numbers with the decimal
 * point of the current LC_NUMERIC locale, which a host is free to set —
 * GUI toolkits do. Under a locale with a decimal comma, `1.5` stopped
 * being a number, `2,5` became one, and `f.` printed a comma. Forth
 * source and ff's output use '.' regardless.
 */

#pragma once

#include <ff_types_p.h>

#include <stdbool.h>
#include <stddef.h>

/**
 * Read @p s as a real literal: an optional '-', then digits with an
 * optional fraction (`1.5`, `1.`) or a fraction alone (`-.5`), then an
 * optional exponent (`e-3`). Nothing else — no `inf` or `nan`, which are
 * left to be words. A value too small to be represented normally reads
 * as the nearest one there is; one too large for a real doesn't read.
 *
 * @param s   NUL-terminated token.
 * @param out The value, when @p s is a real literal.
 * @return true if @p s is a real literal.
 */
bool ff_real_parse(const char *s, ff_real_t *out);

/**
 * snprintf() one real with @p fmt — a format with a single floating-point
 * conversion, such as "%g" or "%.17g" — with '.' as the decimal point.
 *
 * @param buf  Output buffer.
 * @param size Size of @p buf.
 * @param fmt  printf format.
 * @param r    Value.
 * @return As snprintf().
 */
int ff_real_format(char *buf, size_t size, const char *fmt, double r);
