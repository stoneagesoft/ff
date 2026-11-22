/*
 * ff --- math word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Math words
 * =================================================================== */

const ff_word_def_t FF_MATH_WORDS[] =
{
    _FF_W("+", FF_OP_ADD, "( n1 n2 -- n3 )",
      FF_TR_EN("n3 = n1 + n2\n"
               "Adds *n1* and *n2* and leaves sum on stack."),
      FF_TR_RU("n3 = n1 + n2\n"
               "Складывает *n1* и *n2* и оставляет сумму на стеке.")),
    _FF_W("-", FF_OP_SUB, "( n1 n2 -- n3 )",
      FF_TR_EN("n3 = n1 - n2\n"
               "Subtracts *n2* from *n1* and leaves difference on stack."),
      FF_TR_RU("n3 = n1 - n2\n"
               "Вычитает *n2* из *n1* и оставляет разность на стеке.")),
    _FF_W("*", FF_OP_MUL, "( n1 n2 -- n3 )",
      FF_TR_EN("n3 = n1 * n2\n"
               "Multiplies *n1* and *n2* and leaves product on stack."),
      FF_TR_RU("n3 = n1 * n2\n"
               "Умножает *n1* на *n2* и оставляет произведение на стеке.")),
    _FF_W("/", FF_OP_DIV, "( n1 n2 -- n3 )",
      FF_TR_EN("n3 = n1 / n2\n"
               "Divides *n1* by *n2* and leaves quotient on stack."),
      FF_TR_RU("n3 = n1 / n2\n"
               "Делит *n1* на *n2* и оставляет частное на стеке.")),
    _FF_W("mod", FF_OP_MOD, "( n1 n2 -- n3 )",
      FF_TR_EN("Modulus (remainder)\n"
               "The remainder when *n1* is divided by *n2* is left on the top of the stack."),
      FF_TR_RU("Остаток от деления\n"
               "На вершине стека остаётся остаток от деления *n1* на *n2*.")),
    _FF_W("/mod", FF_OP_DIVMOD, "( n1 n2 -- n3 n4 )",
      FF_TR_EN("n3 = n1 mod n2, n4 = n1 / n2\n"
               "Divides *n1* by *n2* and leaves quotient on top of stack, remainder as next on stack."),
      FF_TR_RU("n3 = n1 mod n2, n4 = n1 / n2\n"
               "Делит *n1* на *n2* и оставляет частное на вершине стека, а остаток под ним.")),
    _FF_W("min", FF_OP_MIN, "( n1 n2 -- n3 )",
      FF_TR_EN("Minimum\n"
               "The lesser of *n1* and *n2* is left on the top of the stack."),
      FF_TR_RU("Минимум\n"
               "На вершине стека остаётся меньшее из *n1* и *n2*.")),
    _FF_W("max", FF_OP_MAX, "( n1 n2 -- n3 )",
      FF_TR_EN("Maximum\n"
               "The greater of *n1* and *n2* is left on the top of the stack."),
      FF_TR_RU("Максимум\n"
               "На вершине стека остаётся большее из *n1* и *n2*.")),
    _FF_W("negate", FF_OP_NEGATE, "( n1 -- n2 )",
      FF_TR_EN("Negate\n"
               "Negates the value on the top of the stack."),
      FF_TR_RU("Сменить знак\n"
               "Меняет знак значения на вершине стека.")),
    _FF_W("abs", FF_OP_ABS, "( n1 -- n2 )",
      FF_TR_EN("n2 = |n1|\n"
               "Replaces top of stack with its absolute value."),
      FF_TR_RU("n2 = |n1|\n"
               "Заменяет вершину стека её абсолютным значением.")),
    _FF_W("=", FF_OP_EQ, "( n1 n2 -- flag )",
      FF_TR_EN("Equal\n"
               "Returns *-1* if *n1* = *n2*, *0* otherwise."),
      FF_TR_RU("Равно\n"
               "Возвращает *-1*, если *n1* = *n2*, иначе *0*.")),
    _FF_W("<>", FF_OP_NEQ, "( n1 n2 -- flag )",
      FF_TR_EN("Not equal\n"
               "Returns *-1* if *n1* != *n2*, *0* otherwise."),
      FF_TR_RU("Не равно\n"
               "Возвращает *-1*, если *n1* != *n2*, иначе *0*.")),
    _FF_W("<", FF_OP_LT, "( n1 n2 -- flag )",
      FF_TR_EN("Less than\n"
               "Returns *-1* if *n1* < *n2*, *0* otherwise."),
      FF_TR_RU("Меньше\n"
               "Возвращает *-1*, если *n1* < *n2*, иначе *0*.")),
    _FF_W(">", FF_OP_GT, "( n1 n2 -- flag )",
      FF_TR_EN("Greater\n"
               "Returns *-1* if *n1* > *n2*, *0* otherwise."),
      FF_TR_RU("Больше\n"
               "Возвращает *-1*, если *n1* > *n2*, иначе *0*.")),
    _FF_W("<=", FF_OP_LE, "( n1 n2 -- flag )",
      FF_TR_EN("Less than or equal\n"
               "Returns *-1* if *n1* <= *n2*, *0* otherwise."),
      FF_TR_RU("Меньше или равно\n"
               "Возвращает *-1*, если *n1* <= *n2*, иначе *0*.")),
    _FF_W(">=", FF_OP_GE, "( n1 n2 -- flag )",
      FF_TR_EN("Greater than or equal\n"
               "Returns *-1* if *n1* >= *n2*, *0* otherwise."),
      FF_TR_RU("Больше или равно\n"
               "Возвращает *-1*, если *n1* >= *n2*, иначе *0*.")),
    _FF_W("0=", FF_OP_ZERO_EQ, "( n1 -- flag )",
      FF_TR_EN("Equal to zero\n"
               "Returns *-1* if *n1* is zero, *0* otherwise."),
      FF_TR_RU("Равно нулю\n"
               "Возвращает *-1*, если *n1* равно нулю, иначе *0*.")),
    _FF_W("0<>", FF_OP_ZERO_NEQ, "( n1 -- flag )",
      FF_TR_EN("Nonzero\n"
               "Returns *-1* if *n1* is nonzero, *0* otherwise."),
      FF_TR_RU("Не ноль\n"
               "Возвращает *-1*, если *n1* не равно нулю, иначе *0*.")),
    _FF_W("0>", FF_OP_ZERO_GT, "( n1 -- flag )",
      FF_TR_EN("Greater than zero\n"
               "Returns *-1* if *n1* greater than zero, *0* otherwise."),
      FF_TR_RU("Больше нуля\n"
               "Возвращает *-1*, если *n1* больше нуля, иначе *0*.")),
    _FF_W("0<", FF_OP_ZERO_LT, "( n1 -- flag )",
      FF_TR_EN("Less than zero\n"
               "Returns *-1* if *n1* less than zero, *0* otherwise."),
      FF_TR_RU("Меньше нуля\n"
               "Возвращает *-1*, если *n1* меньше нуля, иначе *0*.")),
    _FF_W("and", FF_OP_AND, "( n1 n2 -- n3 )",
      FF_TR_EN("Bitwise and\n"
               "Stores the bitwise and of *n1* and *n2* on the stack."),
      FF_TR_RU("Побитовое И\n"
               "Кладёт на стек побитовое И *n1* и *n2*.")),
    _FF_W("or", FF_OP_OR, "( n1 n2 -- n3 )",
      FF_TR_EN("Bitwise or\n"
               "Stores the bitwise or of *n1* and *n2* on the stack."),
      FF_TR_RU("Побитовое ИЛИ\n"
               "Кладёт на стек побитовое ИЛИ *n1* и *n2*.")),
    _FF_W("xor", FF_OP_XOR, "( n1 n2 -- n3 )",
      FF_TR_EN("Bitwise exclusive or\n"
               "Stores the bitwise exclusive or of *n1* and *n2* on the stack."),
      FF_TR_RU("Побитовое исключающее ИЛИ\n"
               "Кладёт на стек побитовое исключающее ИЛИ *n1* и *n2*.")),
    _FF_W("not", FF_OP_NOT, "( n1 -- n2 )",
      FF_TR_EN("Logical not\n"
               "Inverts the bits in the value on the top of the stack.\n"
               "This performs logical negation for truth values of\n"
               "*-1* (true) and *0* (false)."),
      FF_TR_RU("Логическое НЕ\n"
               "Инвертирует биты значения на вершине стека.\n"
               "Для логических значений *-1* (истина) и *0* (ложь)\n"
               "это логическое отрицание.")),
    _FF_W("shift", FF_OP_SHIFT, "( n1 n2 -- n3 )",
      FF_TR_EN("Shift n1 by n2 bits\n"
               "The value *n1* is logically shifted the number of bits\n"
               "specified by *n2*, left if *n2* is positive and right\n"
               "if *n2* is negative. Zero bits are shifted into vacated bits."),
      FF_TR_RU("Сдвинуть n1 на n2 бит\n"
               "Значение *n1* логически сдвигается на число бит, заданное *n2*:\n"
               "влево, если *n2* положительно, и вправо, если *n2* отрицательно.\n"
               "Освободившиеся биты заполняются нулями.")),
    _FF_W("1+", FF_OP_INC, "( n1 -- n2 )",
      FF_TR_EN("Add one\n"
               "Adds one to top of stack."),
      FF_TR_RU("Прибавить единицу\n"
               "Прибавляет единицу к вершине стека.")),
    _FF_W("1-", FF_OP_DEC, "( n1 -- n2 )",
      FF_TR_EN("Subtract one\n"
               "Subtracts one from top of stack."),
      FF_TR_RU("Вычесть единицу\n"
               "Вычитает единицу из вершины стека.")),
    _FF_W("2+", FF_OP_INC2, "( n1 -- n2 )",
      FF_TR_EN("Add two\n"
               "Adds two to top of stack."),
      FF_TR_RU("Прибавить два\n"
               "Прибавляет два к вершине стека.")),
    _FF_W("2-", FF_OP_DEC2, "( n1 -- n2 )",
      FF_TR_EN("Subtract two\n"
               "Subtracts two from top of stack."),
      FF_TR_RU("Вычесть два\n"
               "Вычитает два из вершины стека.")),
    _FF_W("2*", FF_OP_MUL2, "( n1 -- n2 )",
      FF_TR_EN("Times two\n"
               "Multiplies the top of stack by two."),
      FF_TR_RU("Умножить на два\n"
               "Умножает вершину стека на два.")),
    _FF_W("2/", FF_OP_DIV2, "( n1 -- n2 )",
      FF_TR_EN("Divide by two\n"
               "Divides top of stack by two."),
      FF_TR_RU("Разделить на два\n"
               "Делит вершину стека на два.")),
    _FF_W("base", FF_OP_SET_BASE, "( n -- )",
      FF_TR_EN("Numeric base\n"
               "Sets numeric base to *n*. Only *10* and *16* are supported at the moment."),
      FF_TR_RU("Основание системы счисления\n"
               "Устанавливает основание системы счисления *n*. Пока поддерживаются\n"
               "только *10* и *16*.")),
    FF_WEND
};

