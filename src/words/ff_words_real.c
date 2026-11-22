/*
 * ff --- real word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Floating point words
 * =================================================================== */

const ff_word_def_t FF_REAL_WORDS[] =
{
    _FF_W("(flit)", FF_OP_FLIT, "( -- f )",
      FF_TR_EN("Push floating point literal\n"
               "Pushes the floating point literal that follows in line onto the top of the stack."),
      FF_TR_RU("Положить вещественный литерал\n"
               "Кладёт на вершину стека вещественное число, записанное в коде сразу за словом.")),
    _FF_W("f+", FF_OP_FADD, "( f1 f2 -- f3 )",
      FF_TR_EN("f3 = f1 + f2\n"
               "The two floating point values on the top of the stack are\n"
               "added and their sum is placed on the top of the stack."),
      FF_TR_RU("f3 = f1 + f2\n"
               "Два вещественных значения на вершине стека складываются,\n"
               "и их сумма кладётся на вершину стека.")),
    _FF_W("f-", FF_OP_FSUB, "( f1 f2 -- f3 )",
      FF_TR_EN("f3 = f1 - f2\n"
               "The floating point value *f2* is subtracted from the floating point\n"
               "value *f1* and the result is placed on the top of the stack."),
      FF_TR_RU("f3 = f1 - f2\n"
               "Вещественное значение *f2* вычитается из вещественного значения *f1*,\n"
               "и результат кладётся на вершину стека.")),
    _FF_W("f*", FF_OP_FMUL, "( f1 f2 -- f3 )",
      FF_TR_EN("f3 = f1 * f2\n"
               "The two floating point values on the top of the stack are\n"
               "multiplied and their product is placed on the top of the stack."),
      FF_TR_RU("f3 = f1 * f2\n"
               "Два вещественных значения на вершине стека перемножаются,\n"
               "и их произведение кладётся на вершину стека.")),
    _FF_W("f/", FF_OP_FDIV, "( f1 f2 -- f3 )",
      FF_TR_EN("f3 = f1 / f2\n"
               "The floating point value *f1* is divided by the floating point\n"
               "value *f2* and the quotient is placed on the top of the stack."),
      FF_TR_RU("f3 = f1 / f2\n"
               "Вещественное значение *f1* делится на вещественное значение *f2*,\n"
               "и частное кладётся на вершину стека.")),
    _FF_W("fnegate", FF_OP_FNEGATE, "( f1 -- f2 )",
      FF_TR_EN("Floating negate\n"
               "The negative of the floating point value on the top\n"
               "of the stack replaces the floating point value there."),
      FF_TR_RU("Сменить знак вещественного\n"
               "Вещественное значение на вершине стека заменяется\n"
               "значением с противоположным знаком.")),
    _FF_W("fabs", FF_OP_FABS, "( f1 -- f2 )",
      FF_TR_EN("f2 = |f1|\n"
               "Replaces floating point top of stack with its absolute value."),
      FF_TR_RU("f2 = |f1|\n"
               "Заменяет вещественное значение на вершине стека его абсолютным значением.")),
    _FF_W("sqrt", FF_OP_FSQRT, "( f1 -- f2 )",
      FF_TR_EN("Square root\n"
               "The floating point value on the top of the stack is\n"
               "replaced by its square root."),
      FF_TR_RU("Квадратный корень\n"
               "Вещественное значение на вершине стека заменяется\n"
               "его квадратным корнем.")),
    _FF_W("sin", FF_OP_FSIN, "( f1 -- f2 )",
      FF_TR_EN("Sine\n"
               "The floating point value on the top of the stack is replaced by its sine."),
      FF_TR_RU("Синус\n"
               "Вещественное значение на вершине стека заменяется его синусом.")),
    _FF_W("cos", FF_OP_FCOS, "( f1 -- f2 )",
      FF_TR_EN("Cosine\n"
               "The floating point value on the top of the stack is replaced by its cosine."),
      FF_TR_RU("Косинус\n"
               "Вещественное значение на вершине стека заменяется его косинусом.")),
    _FF_W("tan", FF_OP_FTAN, "( f1 -- f2 )",
      FF_TR_EN("Tangent\n"
               "The floating point value on the top of the stack is replaced by its tangent."),
      FF_TR_RU("Тангенс\n"
               "Вещественное значение на вершине стека заменяется его тангенсом.")),
    _FF_W("asin", FF_OP_FASIN, "( f1 -- f2 )",
      FF_TR_EN("f2 = arcsin(f1)\n"
               "Replaces floating point top of stack with its arc sine."),
      FF_TR_RU("f2 = arcsin(f1)\n"
               "Заменяет вещественное значение на вершине стека его арксинусом.")),
    _FF_W("acos", FF_OP_FACOS, "( f1 -- f2 )",
      FF_TR_EN("f2 = arccos(f1)\n"
               "Replaces floating point top of stack with its arc cosine."),
      FF_TR_RU("f2 = arccos(f1)\n"
               "Заменяет вещественное значение на вершине стека его арккосинусом.")),
    _FF_W("atan", FF_OP_FATAN, "( f1 -- f2 )",
      FF_TR_EN("f2 = arctan(f1)\n"
               "Replaces floating point top of stack with its arc tangent."),
      FF_TR_RU("f2 = arctan(f1)\n"
               "Заменяет вещественное значение на вершине стека его арктангенсом.")),
    _FF_W("atan2", FF_OP_FATAN2, "( f1 f2 -- f3 )",
      FF_TR_EN("f3 = arctan(f1 / f2)\n"
               "Replaces the two floating point numbers on the top of the\n"
               "stack with the arc tangent of their quotient, properly\n"
               "handling zero denominators."),
      FF_TR_RU("f3 = arctan(f1 / f2)\n"
               "Заменяет два вещественных числа на вершине стека арктангенсом\n"
               "их частного, правильно обрабатывая нулевой знаменатель.")),
    _FF_W("exp", FF_OP_FEXP, "( f1 -- f2 )",
      FF_TR_EN("f2 = e ^ f1\n"
               "The floating point value on the top of the stack is replaced\n"
               "by its natural antilogarithm."),
      FF_TR_RU("f2 = e ^ f1\n"
               "Вещественное значение на вершине стека заменяется\n"
               "его натуральным антилогарифмом.")),
    _FF_W("log", FF_OP_FLOG, "( f1 -- f2 )",
      FF_TR_EN("Natural logarithm\n"
               "The floating point value on the top of the stack is replaced\n"
               "by its natural logarithm."),
      FF_TR_RU("Натуральный логарифм\n"
               "Вещественное значение на вершине стека заменяется\n"
               "его натуральным логарифмом.")),
    _FF_W("pow", FF_OP_FPOW, "( f1 f2 -- f3 )",
      FF_TR_EN("f3 = f1 ^ f2\n"
               "The second floating point value on the stack is taken to\n"
               "the power of the top floating point stack value and the\n"
               "result is left on the top of the stack."),
      FF_TR_RU("f3 = f1 ^ f2\n"
               "Второе вещественное значение стека возводится в степень,\n"
               "равную вещественному значению на вершине, и результат\n"
               "остаётся на вершине стека.")),
    _FF_W("f.", FF_OP_F_DOT, "( f -- )",
      FF_TR_EN("Print floating point\n"
               "The floating point value on the top of the stack is printed."),
      FF_TR_RU("Напечатать вещественное\n"
               "Печатает вещественное значение с вершины стека.")),
    _FF_W("float", FF_OP_FLOAT, "( n -- f )",
      FF_TR_EN("Integer to floating\n"
               "The integer value on the top of the stack is replaced by\n"
               "the equivalent floating point value."),
      FF_TR_RU("Целое в вещественное\n"
               "Целое значение на вершине стека заменяется\n"
               "равным ему вещественным значением.")),
    _FF_W("fix", FF_OP_FIX, "( f -- n )",
      FF_TR_EN("Floating to integer\n"
               "The floating point number on the top of the stack is replaced\n"
               "by the integer obtained by truncating its fractional part. One\n"
               "whose integer part doesn't fit a cell, or NaN, is an error (-24)."),
      FF_TR_RU("Вещественное в целое\n"
               "Вещественное число на вершине стека заменяется целым,\n"
               "полученным отбрасыванием дробной части. Число, целая часть которого\n"
               "не помещается в ячейку, и NaN — ошибка (-24).")),
    _FF_W("pi", FF_OP_PI, "( -- pi )",
      FF_TR_EN("Pi constant\n"
               "Returns *Pi* constant."),
      FF_TR_RU("Константа пи\n"
               "Возвращает константу *Pi*.")),
    _FF_W("e", FF_OP_E_CONST, "( -- e )",
      FF_TR_EN("e constant\n"
               "Returns *e* constant, the base of the natural logarithm."),
      FF_TR_RU("Константа e\n"
               "Возвращает константу *e*, основание натурального логарифма.")),
    _FF_W("f=", FF_OP_FEQ, "( f1 f2 -- flag )",
      FF_TR_EN("Floating equal\n"
               "The top of stack is set to *-1* if *f1* is equal to *f2* and *0* otherwise."),
      FF_TR_RU("Вещественное равно\n"
               "На вершину стека кладётся *-1*, если *f1* равно *f2*, иначе *0*.")),
    _FF_W("f<>", FF_OP_FNEQ, "( f1 f2 -- flag )",
      FF_TR_EN("Floating not equal\n"
               "The top of stack is set to *-1* if *f1* is not equal to *f2* and *0* otherwise."),
      FF_TR_RU("Вещественное не равно\n"
               "На вершину стека кладётся *-1*, если *f1* не равно *f2*, иначе *0*.")),
    _FF_W("f<", FF_OP_FLT, "( f1 f2 -- flag )",
      FF_TR_EN("Floating less than\n"
               "The top of stack is set to *-1* if *f1* is less than *f2* and *0* otherwise."),
      FF_TR_RU("Вещественное меньше\n"
               "На вершину стека кладётся *-1*, если *f1* меньше *f2*, иначе *0*.")),
    _FF_W("f>", FF_OP_FGT, "( f1 f2 -- flag )",
      FF_TR_EN("Floating greater than\n"
               "The top of stack is set to *-1* if *f1* is greater than *f2* and *0* otherwise."),
      FF_TR_RU("Вещественное больше\n"
               "На вершину стека кладётся *-1*, если *f1* больше *f2*, иначе *0*.")),
    _FF_W("f<=", FF_OP_FLE, "( f1 f2 -- flag )",
      FF_TR_EN("Floating less than or equal\n"
               "The top of stack is set to *-1* if *f1* is less than or equal to *f2* and *0* otherwise."),
      FF_TR_RU("Вещественное меньше или равно\n"
               "На вершину стека кладётся *-1*, если *f1* меньше или равно *f2*, иначе *0*.")),
    _FF_W("f>=", FF_OP_FGE, "( f1 f2 -- flag )",
      FF_TR_EN("Floating greater than or equal\n"
               "The top of stack is set to *-1* if *f1* is greater than or equal to *f2* and *0* otherwise."),
      FF_TR_RU("Вещественное больше или равно\n"
               "На вершину стека кладётся *-1*, если *f1* больше или равно *f2*, иначе *0*.")),
    FF_WEND
};

