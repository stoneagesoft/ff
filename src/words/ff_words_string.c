/*
 * ff --- string word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * String words
 * =================================================================== */

const ff_word_def_t FF_STRING_WORDS[] =
{
    _FF_W("(strlit)", FF_OP_STRLIT, "( -- s )",
      FF_TR_EN("String literal\n"
               "Pushes the address of the string literal that follows in line onto the stack."),
      FF_TR_RU("Строковый литерал\n"
               "Кладёт на стек адрес строкового литерала, записанного в коде сразу за словом.")),
    _FF_W("string", FF_OP_STRING, "w ( size -- )",
      FF_TR_EN("Declare string\n"
               "Declares a string named *w* of a maximum of *size - 1* characters."),
      FF_TR_RU("Объявить строку\n"
               "Объявляет строку с именем *w* длиной не больше *size - 1* символов.")),
    _FF_W("s!", FF_OP_S_STORE, "( s1 s2 -- )",
      FF_TR_EN("Store string\n"
               "The string at address *s1* is copied into the string at *s2*."),
      FF_TR_RU("Записать строку\n"
               "Строка по адресу *s1* копируется в строку по адресу *s2*.")),
    _FF_W("s+", FF_OP_S_CAT, "( s1 s2 -- )",
      FF_TR_EN("String concatenate\n"
               "The string at address *s1* is concatenated to the string at address *s2*."),
      FF_TR_RU("Сцепить строки\n"
               "Строка по адресу *s1* дописывается к строке по адресу *s2*.")),
    _FF_W("strlen", FF_OP_STRLEN, "( s -- n )",
      FF_TR_EN("String length\n"
               "The length of string *s* is placed on the top of the stack."),
      FF_TR_RU("Длина строки\n"
               "Кладёт на вершину стека длину строки *s*.")),
    _FF_W("strcmp", FF_OP_STRCMP, "( s1 s2 -- n )",
      FF_TR_EN("String compare\n"
               "The string at address *s1* is compared to the string at address *s2*.\n"
               "Returns an integer less than, equal to, or greater than zero if *s1*\n"
               "is found, respectively, to be less than, to match,\n"
               "or be greater than *s2*."),
      FF_TR_RU("Сравнить строки\n"
               "Строка по адресу *s1* сравнивается со строкой по адресу *s2*.\n"
               "Возвращает целое число меньше нуля, ноль или больше нуля,\n"
               "если *s1* соответственно меньше *s2*, совпадает с ней или больше неё.")),
    FF_WEND
};

