/*
 * ff --- array word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


const ff_word_def_t FF_ARRAY_WORDS[] =
{
    _FF_W("array", FF_OP_ARRAY, "w ( n -- )",
      FF_TR_EN("Declare array\n"
               "Declares an array *w* of *n* elements. Element size is stack item size.\n"
               "When *w* is executed, it gets index from the stack's top and puts\n"
               "address of the element at that index on the stack."),
      FF_TR_RU("Объявить массив\n"
               "Объявляет массив *w* из *n* элементов. Размер элемента равен размеру\n"
               "элемента стека. При выполнении *w* берёт с вершины стека индекс и кладёт\n"
               "на стек адрес элемента с этим индексом.")),
    FF_WEND
};
