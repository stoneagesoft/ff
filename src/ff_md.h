/**
 * @file ff_md.h
 * @brief Markdown renderer for terminal output.
 *
 * Two snprintf-shaped functions render CommonMark (with GFM
 * extensions: tables, strikethrough, autolinks) to a caller-supplied
 * char buffer. The plain variant emits UTF-8 text only; the VT
 * variant adds ANSI escape codes for headings, emphasis, code, and
 * links. Both share the same parser (vendored md4c) and produce
 * output suitable for direct emission via the engine's
 * `ff_platform_t::vprintf` callback.
 */

#pragma once

#include <stddef.h>


/**
 * @brief Render Markdown to plain UTF-8 text.
 *
 * Snprintf-shaped: always NUL-terminates when @p size > 0,
 * truncates at @p size - 1 bytes, and returns the number of bytes
 * that *would have been written* excluding the terminator. To
 * pre-size the output: call once with `buf=NULL, size=0`, allocate
 * `return + 1`, then call again.
 *
 * Headings render as bold-uppercase + blank line. Lists get bullet
 * (`*`) or number (`1. `) prefixes with hanging indent. Code blocks
 * are indented 4 spaces. Blockquotes get a `> ` prefix. Hyperlinks
 * render as `text (url)`.
 *
 * @param buf    Destination buffer; may be NULL when @p size is 0.
 * @param size   Capacity of @p buf in bytes.
 * @param md     Source Markdown text (NUL-terminated).
 * @param width  Word-wrap column. Pass 0 to disable wrapping.
 * @return Number of bytes that would have been written, excluding
 *         the NUL terminator.
 */
int ff_md_snprintf(char *buf, size_t size, const char *md, int width);

/**
 * @brief Render Markdown with ANSI escape codes for the terminal.
 *
 * Same contract as @ref ff_md_snprintf. Adds the following styling:
 *
 * | Element         | Sequence                          |
 * |-----------------|-----------------------------------|
 * | h1              | `\033[1;4m` … `\033[0m` (bold + underline) |
 * | h2-h6           | `\033[1m`  … `\033[0m` (bold) |
 * | `**strong**`    | `\033[1m`  … `\033[22m`       |
 * | `*emphasis*`    | `\033[3m`  … `\033[23m`       |
 * | `` `code` ``    | `\033[36m` … `\033[39m` (cyan) |
 * | code blocks     | cyan + 4-space indent         |
 * | `[txt](url)`    | underlined text + dim URL in parens |
 * | `~~strike~~`    | `\033[9m`  … `\033[29m`       |
 *
 * Width counts visible columns; escape sequences are excluded from
 * the wrap calculation.
 */
int ff_md_vt_snprintf(char *buf, size_t size, const char *md, int width);
