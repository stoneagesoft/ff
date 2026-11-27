/*
 * ff --- heap word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Heap words
 * =================================================================== */

const ff_word_def_t FF_HEAP_WORDS[] =
{
    _FF_W("here", FF_OP_HERE, "( -- addr )",
      FF_TR_EN("Heap address\n"
               "The current heap allocation address is placed on the top of the stack."),
      FF_TR_RU("Адрес кучи\n"
               "Кладёт на вершину стека текущий адрес выделения в куче.")),
    _FF_W("unused", FF_OP_UNUSED, "( -- u )",
      FF_TR_EN("Memory left\n"
               "Leaves the number of bytes Forth code can still make the engine\n"
               "hold: the host's memory limit less what the engine holds now.\n"
               "Memory for words and strings is taken a slab at a time, so the\n"
               "number falls in steps. Without a limit, the largest positive number."),
      FF_TR_RU("Свободная память\n"
               "Кладёт, сколько ещё байт может занять код Форта: предел памяти\n"
               "хоста минус то, что движок уже держит. Память под слова и строки\n"
               "берётся кусками, поэтому число убывает ступенями. Без предела —\n"
               "наибольшее положительное число.")),
    _FF_W("!", FF_OP_STORE, "( n addr -- )",
      FF_TR_EN("Store into address\n"
               "Stores the value *n* into the address *addr*."),
      FF_TR_RU("Записать по адресу\n"
               "Записывает значение *n* по адресу *addr*.")),
    _FF_W("@", FF_OP_FETCH, "( addr -- n )",
      FF_TR_EN("Load\n"
               "Loads the value at *addr* and leaves it at the top of the stack."),
      FF_TR_RU("Прочитать\n"
               "Читает значение по адресу *addr* и оставляет его на вершине стека.")),
    _FF_W("+!", FF_OP_PLUS_STORE, "( n addr -- )",
      FF_TR_EN("Add indirect\n"
               "Adds *n* to the word at address *addr*."),
      FF_TR_RU("Прибавить по адресу\n"
               "Прибавляет *n* к ячейке по адресу *addr*.")),
    _FF_W("allot", FF_OP_ALLOT, "( n -- )",
      FF_TR_EN("Allocate heap\n"
               "Allocates *n* bytes of heap space."),
      FF_TR_RU("Выделить в куче\n"
               "Выделяет в куче *n* байт.")),
    _FF_W(",", FF_OP_COMMA, "( n -- )",
      FF_TR_EN("Store in heap\n"
               "Reserves one cell of heap space, initializing it to *n*."),
      FF_TR_RU("Записать в кучу\n"
               "Резервирует в куче одну ячейку и записывает в неё *n*.")),
    _FF_W("c!", FF_OP_C_STORE, "( n addr -- )",
      FF_TR_EN("Store byte\n"
               "The 8 bit value *n* is stored in the byte at address *addr*."),
      FF_TR_RU("Записать байт\n"
               "8-битное значение *n* записывается в байт по адресу *addr*.")),
    _FF_W("c@", FF_OP_C_FETCH, "( addr -- n )",
      FF_TR_EN("Load byte\n"
               "The byte at address *addr* is placed on the top of the stack."),
      FF_TR_RU("Прочитать байт\n"
               "Байт по адресу *addr* кладётся на вершину стека.")),
    _FF_W("c,", FF_OP_C_COMMA, "( n -- )",
      FF_TR_EN("Compile byte\n"
               "The 8 bit value *n* is stored in the next free byte of the\n"
               "heap and the heap pointer is incremented by one."),
      FF_TR_RU("Скомпилировать байт\n"
               "8-битное значение *n* записывается в следующий свободный байт кучи,\n"
               "и указатель кучи увеличивается на единицу.")),
    _FF_W("c=", FF_OP_C_ALIGN, "( -- )",
      FF_TR_EN("Align heap\n"
               "The heap allocation pointer is adjusted to the next\n"
               "eight byte boundary. This must be done following a sequence of *c,*"),
      FF_TR_RU("Выровнять кучу\n"
               "Указатель выделения в куче сдвигается к следующей границе\n"
               "в восемь байт. Это нужно делать после последовательности *c,*")),
    FF_WEND
};

