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

    ft_table_t *table = ff_table_new(ff);
    if (!table)
        return;
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

    ff_table_print(ff, table, "");
}

#ifdef FF_OS_UNIX
void ff_print_memstat(ff_t *ff)
{
    const char *statm_path = "/proc/self/statm";

    FILE *f = fopen(statm_path, "r");
    if (!f)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNSUPPORTED,
                  FF_TR(ff, FF_TR_EN("Failed to open file '%s': %s"),
                            FF_TR_RU("Не удалось открыть файл «%s»: %s")),
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
                  FF_TR(ff, FF_TR_EN("Invalid or unsupported content of '%s'."),
                            FF_TR_RU("Неверное или неподдерживаемое содержимое «%s».")),
                  statm_path);
        return;
    }

    const long page_size = sysconf(_SC_PAGE_SIZE);

    ft_table_t *table = ff_table_new(ff);
    if (!table)
        return;
    ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(table, 0, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_set_cell_prop(table, 0, 1, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_u8write_ln(table,
                  FF_TR(ff, FF_TR_EN("Memory area"), FF_TR_RU("Область памяти")),
                  FF_TR(ff, FF_TR_EN("Size in bytes"), FF_TR_RU("Размер, байт")));
    ft_u8printf_ln(table, "%s|%ld",
                   FF_TR(ff, FF_TR_EN("Resident"), FF_TR_RU("Резидентная")),
                   (long)(resident * page_size));
    ft_u8printf_ln(table, "%s|%ld",
                   FF_TR(ff, FF_TR_EN("Shared"), FF_TR_RU("Разделяемая")),
                   (long)(shared * page_size));
    ft_u8printf_ln(table, "%s|%ld",
                   FF_TR(ff, FF_TR_EN("Code"), FF_TR_RU("Код")),
                   (long)(text * page_size));
    ft_u8printf_ln(table, "%s|%ld",
                   FF_TR(ff, FF_TR_EN("Data + stack"), FF_TR_RU("Данные и стек")),
                   (long)(data * page_size));
    ft_add_separator(table);
    ft_u8printf_ln(table, "%s|%ld",
                   FF_TR(ff, FF_TR_EN("Total"), FF_TR_RU("Всего")),
                   (long)(size * page_size));

    ff_table_print(ff, table, "");
}
#endif


const ff_word_def_t FF_DEBUG_WORDS[] =
{
    _FF_W("trace", FF_OP_TRACE, "( n -- )",
      FF_TR_EN("Trace mode\n"
               "If *n* is nonzero, trace mode is enabled. If *n* is zero, trace mode is turned off."),
      FF_TR_RU("Режим трассировки\n"
               "Если *n* не равно нулю, трассировка включается. Если *n* равно нулю,\n"
               "трассировка выключается.")),
    _FF_W("backtrace", FF_OP_BACKTRACE, "( n -- )",
      FF_TR_EN("Walk-back mode\n"
               "If *n* is nonzero, a back trace through active words\n"
               "will be performed whenever an error occurs during execution.\n"
               "If *n* is zero, the back trace is suppressed."),
      FF_TR_RU("Обратная трассировка\n"
               "Если *n* не равно нулю, при каждой ошибке во время выполнения\n"
               "печатается цепочка активных слов.\n"
               "Если *n* равно нулю, цепочка не печатается.")),
    _FF_W("dump", FF_OP_DUMP, "( a n -- )",
      FF_TR_EN("Memory dump\n"
               "Print memory dump *n* bytes in length starting at address *a*."),
      FF_TR_RU("Дамп памяти\n"
               "Печатает дамп памяти длиной *n* байт, начиная с адреса *a*.")),
#ifdef FF_OS_UNIX
    _FF_W("memstat", FF_OP_MEMSTAT, "( -- )",
      FF_TR_EN("Print memory status\n"
               "Prints current memory usage of the process\n"
               "(resident set, shared, code, data, total)."),
      FF_TR_RU("Состояние памяти\n"
               "Печатает, сколько памяти сейчас занимает процесс\n"
               "(резидентная, разделяемая, код, данные, всего).")),
#endif
    FF_WEND
};
