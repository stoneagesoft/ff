/*
 * ff --- conio word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <fort/fort.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Console I/O words
 * =================================================================== */

const ff_word_def_t FF_CONIO_WORDS[] =
{
    _FF_W(".", FF_OP_DOT, "( n -- )",
      FF_TR_EN("Print top of stack\n"
               "Prints the number on the top of the stack."),
      FF_TR_RU("Напечатать вершину стека\n"
               "Печатает число с вершины стека.")),
    _FF_W("?", FF_OP_QUESTION, "( addr -- )",
      FF_TR_EN("Print indirect\n"
               "Prints the value at the address at the top of the stack."),
      FF_TR_RU("Напечатать по адресу\n"
               "Печатает значение по адресу с вершины стека.")),
    _FF_W("cr", FF_OP_CR, "( -- )",
      FF_TR_EN("Carriage return\n"
               "The standard output stream is advanced to the first character of the next line."),
      FF_TR_RU("Перевод строки\n"
               "Переводит стандартный вывод на начало следующей строки.")),
    _FF_W("emit", FF_OP_EMIT, "( c -- )",
      FF_TR_EN("Emit character\n"
               "The character *c* is printed on standard output."),
      FF_TR_RU("Вывести символ\n"
               "Печатает символ *c* в стандартный вывод.")),
    _FF_W("type", FF_OP_TYPE, "( s -- )",
      FF_TR_EN("Print string\n"
               "The string at address *s* is printed on standard output."),
      FF_TR_RU("Напечатать строку\n"
               "Печатает строку по адресу *s* в стандартный вывод.")),
    _FF_W(".s", FF_OP_DOT_S, "( -- )",
      FF_TR_EN("Print stack\n"
               "Prints entire contents of stack."),
      FF_TR_RU("Напечатать стек\n"
               "Печатает всё содержимое стека.")),
    _FF_WI(".\"", FF_OP_DOTQUOTE, "s ( -- )",
      FF_TR_EN("Print immediate string\n"
               "Compiles the string literal *s* that follows in line into the\n"
               "current definition, to be printed when the definition runs."),
      FF_TR_RU("Напечатать строку из кода\n"
               "Компилирует строковый литерал *s*, записанный сразу за словом,\n"
               "в текущее определение; он напечатается, когда определение выполнится.")),
    _FF_WI(".(", FF_OP_DOT_PAREN, "s ( -- )",
      FF_TR_EN("Print constant string\n"
               "Immediately prints the string literal *s* that follows in the input\n"
               "stream, inside a definition too: it prints while the definition is\n"
               "compiled and adds nothing to it."),
      FF_TR_RU("Напечатать строку сразу\n"
               "Сразу печатает строковый литерал *s*, следующий во входном потоке, —\n"
               "и внутри определения тоже: он печатается во время компиляции\n"
               "определения и ничего в него не добавляет.")),
    FF_WEND
};

