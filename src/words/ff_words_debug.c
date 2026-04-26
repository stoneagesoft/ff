/*
 * ff --- debug word definitions.
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

#ifdef FF_OS_UNIX
#   include <unistd.h>
#endif


/* ===================================================================
 * Debug words
 * =================================================================== */

void ff_dump_bytes(ff_t *ff, const char *addr, size_t size)
{
    if (size == 0)
        return;

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

#ifdef FF_OS_UNIX
void ff_print_memstat(ff_t *ff)
{
    const char *statm_path = "/proc/self/statm";

    FILE *f = fopen(statm_path, "r");
    if (!f)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNSUPPORTED,
                  "Failed to open file '%s': %s",
                  statm_path, strerror(errno));
        return;
    }

    unsigned long size, resident, shared, text, data;
    unsigned long dummy;
    int n = fscanf(f, "%lu %lu %lu %lu %lu %lu %lu",
                   &size, &resident, &shared, &text, &dummy, &data, &dummy);
    fclose(f);
    if (n != 7)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNSUPPORTED,
                  "Invalid or unsupported content of '%s'.",
                  statm_path);
        return;
    }

    const long page_size = sysconf(_SC_PAGE_SIZE);

    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_ROUND_STYLE);
    ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(table, 0, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_set_cell_prop(table, 0, 1, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_u8write_ln(table, "Memory area", "Size in bytes");
    ft_u8printf_ln(table, "Resident|%ld", (long)(resident * page_size));
    ft_u8printf_ln(table, "Shared|%ld", (long)(shared * page_size));
    ft_u8printf_ln(table, "Code|%ld", (long)(text * page_size));
    ft_u8printf_ln(table, "Data + stack|%ld", (long)(data * page_size));
    ft_add_separator(table);
    ft_u8printf_ln(table, "Total|%ld", (long)(size * page_size));

    ff_printf(ff, "%s", (const char *)ft_to_u8string(table));
    ft_destroy_table(table);
}
#endif


const ff_word_def_t FF_DEBUG_WORDS[] =
{
    _FF_W("trace", FF_OP_TRACE,
      "( n -- )  Trace mode\n"
      "If *n* is nonzero, trace mode is enabled. If *n* is zero, trace mode is turned off."),
    _FF_W("backtrace", FF_OP_BACKTRACE,
      "( n -- )  Walk-back mode\n"
      "If *n* is nonzero, a back trace through active words\n"
      "will be performed whenever an error occurs during execution.\n"
      "If *n* is zero, the back trace is suppressed."),
    _FF_W("dump", FF_OP_DUMP,
      "( a n -- )  Memory dump\n"
      "Print memory dump *n* bytes in length starting at address *a*."),
    _FF_W("ERRNO", FF_OP_ERRNO,
      "( -- errno )  C standard library error\n"
      "Gives a read-only access to the *errno* C variable.\n"
      "\n"
      "See also: **strerror**"),
#ifdef FF_OS_UNIX
    _FF_W("memstat", FF_OP_MEMSTAT,
      "( -- )  Print memory status\n"
      "Prints current memory usage of the process\n"
      "(resident set, shared, code, data, total)."),
#endif
    FF_WEND
};
