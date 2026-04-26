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
    _FF_W("(flit)", FF_OP_FLIT,
      "( -- f )  Push floating point literal\n"
      "Pushes the floating point literal that follows in line onto the top of the stack."),
    _FF_W("f+", FF_OP_FADD,
      "( f1 f2 -- f3 )  f3 = f1 + f2\n"
      "The two floating point values on the top of the stack are\n"
      "added and their sum is placed on the top of the stack."),
    _FF_W("f-", FF_OP_FSUB,
      "( f1 f2 -- f3 )  f3 = f1 - f2\n"
      "The floating point value *f2* is subtracted from the floating point\n"
      "value *f1* and the result is placed on the top of the stack."),
    _FF_W("f*", FF_OP_FMUL,
      "( f1 f2 -- f3 )  f3 = f1 * f2\n"
      "The two floating point values on the top of the stack are\n"
      "multiplied and their product is placed on the top of the stack."),
    _FF_W("f/", FF_OP_FDIV,
      "( f1 f2 -- f3 )  f3 = f1 / f2\n"
      "The floating point value *f1* is divided by the floating point\n"
      "value *f2* and the quotient is placed on the top of the stack."),
    _FF_W("fnegate", FF_OP_FNEGATE,
      "( f1 -- f2 )  Floating negate\n"
      "The negative of the floating point value on the top\n"
      "of the stack replaces the floating point value there."),
    _FF_W("fabs", FF_OP_FABS,
      "( f1 -- f2 )  f2 = |f1|\n"
      "Replaces floating point top of stack with its absolute value."),
    _FF_W("sqrt", FF_OP_FSQRT,
      "( f1 -- f2 )  Square root\n"
      "The floating point value on the top of the stack is\n"
      "replaced by its square root."),
    _FF_W("sin", FF_OP_FSIN,
      "( f1 -- f2 )  Sine\n"
      "The floating point value on the top of the stack is replaced by its sine."),
    _FF_W("cos", FF_OP_FCOS,
      "( f1 -- f2 )  Cosine\n"
      "The floating point value on the top of the stack is replaced by its cosine."),
    _FF_W("tan", FF_OP_FTAN,
      "( f1 -- f2 )  Tangent\n"
      "The floating point value on the top of the stack is replaced by its tangent."),
    _FF_W("asin", FF_OP_FASIN,
      "( f1 -- f2 )  f2 = arcsin(f1)\n"
      "Replaces floating point top of stack with its arc sine."),
    _FF_W("acos", FF_OP_FACOS,
      "( f1 -- f2 )  f2 = arccos(f1)\n"
      "Replaces floating point top of stack with its arc cosine."),
    _FF_W("atan", FF_OP_FATAN,
      "( f1 -- f2 )  f2 = arctan(f1)\n"
      "Replaces floating point top of stack with its arc tangent."),
    _FF_W("atan2", FF_OP_FATAN2,
      "( f1 f2 -- f3 )  f3 = arctan(f1 / f2)\n"
      "Replaces the two floating point numbers on the top of the\n"
      "stack with the arc tangent of their quotient, properly\n"
      "handling zero denominators."),
    _FF_W("exp", FF_OP_FEXP,
      "( f1 -- f2 )  f2 = e ^ f1\n"
      "The floating point value on the top of the stack is replaced\n"
      "by its natural antilogarithm."),
    _FF_W("log", FF_OP_FLOG,
      "( f1 -- f2 )  Natural logarithm\n"
      "The floating point value on the top of the stack is replaced\n"
      "by its natural logarithm."),
    _FF_W("pow", FF_OP_FPOW,
      "( f1 f2 -- f3 )  f3 = f1 ^ f2\n"
      "The second floating point value on the stack is taken to\n"
      "the power of the top floating point stack value and the\n"
      "result is left on the top of the stack."),
    _FF_W("f.", FF_OP_F_DOT,
      "( f -- )  Print floating point\n"
      "The floating point value on the top of the stack is printed."),
    _FF_W("float", FF_OP_FLOAT,
      "( n -- f )  Integer to floating\n"
      "The integer value on the top of the stack is replaced by\n"
      "the equivalent floating point value."),
    _FF_W("fix", FF_OP_FIX,
      "( f -- n )  Floating to integer\n"
      "The floating point number on the top of the stack is replaced\n"
      "by the integer obtained by truncating its fractional part."),
    _FF_W("pi", FF_OP_PI,
      "( -- pi )  Pi constant\n"
      "Returns *Pi* constant."),
    _FF_W("e", FF_OP_E_CONST,
      "( -- e )  e constant\n"
      "Returns *e* constant, the base of the natural logarithm."),
    _FF_W("f=", FF_OP_FEQ,
      "( f1 f2 -- flag )  Floating equal\n"
      "The top of stack is set to *-1* if *f1* is equal to *f2* and *0* otherwise."),
    _FF_W("f<>", FF_OP_FNEQ,
      "( f1 f2 -- flag )  Floating not equal\n"
      "The top of stack is set to *-1* if *f1* is not equal to *f2* and *0* otherwise."),
    _FF_W("f<", FF_OP_FLT,
      "( f1 f2 -- flag )  Floating less than\n"
      "The top of stack is set to *-1* if *f1* is less than *f2* and *0* otherwise."),
    _FF_W("f>", FF_OP_FGT,
      "( f1 f2 -- flag )  Floating greater than\n"
      "The top of stack is set to *-1* if *f1* is greater than *f2* and *0* otherwise."),
    _FF_W("f<=", FF_OP_FLE,
      "( f1 f2 -- flag )  Floating less than or equal\n"
      "The top of stack is set to *-1* if *f1* is less than or equal to *f2* and *0* otherwise."),
    _FF_W("f>=", FF_OP_FGE,
      "( f1 f2 -- flag )  Floating greater than or equal\n"
      "The top of stack is set to *-1* if *f1* is greater than or equal to *f2* and *0* otherwise."),
    FF_WEND
};

