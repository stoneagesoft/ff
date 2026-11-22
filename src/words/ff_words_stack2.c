/*
 * ff --- stack2 word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Double-stack words
 * =================================================================== */

const ff_word_def_t FF_STACK2_WORDS[] =
{
    _FF_W("2dup", FF_OP_2DUP, "( n1 n2 -- n1 n2 n1 n2 )",
      FF_TR_EN("Duplicate two\n"
               "Duplicates the top two items on the stack."),
      FF_TR_RU("Дублировать пару\n"
               "Дублирует два верхних элемента стека.")),
    _FF_W("2drop", FF_OP_2DROP, "( n1 n2 -- )",
      FF_TR_EN("Double drop\n"
               "Discards the two top items from the stack."),
      FF_TR_RU("Сбросить пару\n"
               "Отбрасывает два верхних элемента стека.")),
    _FF_W("2swap", FF_OP_2SWAP, "( n1 n2 n3 n4 -- n3 n4 n1 n2 )",
      FF_TR_EN("Double swap\n"
               "Swaps the first and second pairs on the stack."),
      FF_TR_RU("Обменять пары\n"
               "Меняет местами первую и вторую пары элементов стека.")),
    _FF_W("2over", FF_OP_2OVER, "( n1 n2 n3 n4 -- n1 n2 n3 n4 n1 n2 )",
      FF_TR_EN("Double over\n"
               "Copies the second pair of items on the stack to the top of stack."),
      FF_TR_RU("Копировать вторую пару\n"
               "Копирует вторую пару элементов стека на вершину.")),
    FF_WEND
};

