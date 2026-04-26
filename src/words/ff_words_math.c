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
    _FF_W("+", FF_OP_ADD,
      "( n1 n2 -- n3 )  n3 = n1 + n2\n"
      "Adds *n1* and *n2* and leaves sum on stack."),
    _FF_W("-", FF_OP_SUB,
      "( n1 n2 -- n3 )  n3 = n1 - n2\n"
      "Subtracts *n2* from *n1* and leaves difference on stack."),
    _FF_W("*", FF_OP_MUL,
      "( n1 n2 -- n3 )  n3 = n1 * n2\n"
      "Multiplies *n1* and *n2* and leaves product on stack."),
    _FF_W("/", FF_OP_DIV,
      "( n1 n2 -- n3 )  n3 = n1 / n2\n"
      "Divides *n1* by *n2* and leaves quotient on stack."),
    _FF_W("mod", FF_OP_MOD,
      "( n1 n2 -- n3 )  Modulus (remainder)\n"
      "The remainder when *n1* is divided by *n2* is left on the top of the stack."),
    _FF_W("/mod", FF_OP_DIVMOD,
      "( n1 n2 -- n3 n4 )  n3 = n1 mod n2, n4 = n1 / n2\n"
      "Divides *n1* by *n2* and leaves quotient on top of stack, remainder as next on stack."),
    _FF_W("min", FF_OP_MIN,
      "( n1 n2 -- n3 )  Minimum\n"
      "The lesser of *n1* and *n2* is left on the top of the stack."),
    _FF_W("max", FF_OP_MAX,
      "( n1 n2 -- n3 )  Maximum\n"
      "The greater of *n1* and *n2* is left on the top of the stack."),
    _FF_W("negate", FF_OP_NEGATE,
      "( n1 -- n2 )  Negate\n"
      "Negates the value on the top of the stack."),
    _FF_W("abs", FF_OP_ABS,
      "( n1 -- n2 )  n2 = |n1|\n"
      "Replaces top of stack with its absolute value."),
    _FF_W("=", FF_OP_EQ,
      "( n1 n2 -- flag )  Equal\n"
      "Returns *-1* if *n1* = *n2*, *0* otherwise."),
    _FF_W("<>", FF_OP_NEQ,
      "( n1 n2 -- flag )  Not equal\n"
      "Returns *-1* if *n1* != *n2*, *0* otherwise."),
    _FF_W("<", FF_OP_LT,
      "( n1 n2 -- flag )  Less than\n"
      "Returns *-1* if *n1* < *n2*, *0* otherwise."),
    _FF_W(">", FF_OP_GT,
      "( n1 n2 -- flag )  Greater\n"
      "Returns *-1* if *n1* > *n2*, *0* otherwise."),
    _FF_W("<=", FF_OP_LE,
      "( n1 n2 -- flag )  Less than or equal\n"
      "Returns *-1* if *n1* <= *n2*, *0* otherwise."),
    _FF_W(">=", FF_OP_GE,
      "( n1 n2 -- flag )  Greater than or equal\n"
      "Returns *-1* if *n1* >= *n2*, *0* otherwise."),
    _FF_W("0=", FF_OP_ZERO_EQ,
      "( n1 -- flag )  Equal to zero\n"
      "Returns *-1* if *n1* is zero, *0* otherwise."),
    _FF_W("0<>", FF_OP_ZERO_NEQ,
      "( n1 -- flag )  Nonzero\n"
      "Returns *-1* if *n1* is nonzero, *0* otherwise."),
    _FF_W("0>", FF_OP_ZERO_GT,
      "( n1 -- flag )  Greater than zero\n"
      "Returns *-1* if *n1* greater than zero, *0* otherwise."),
    _FF_W("0<", FF_OP_ZERO_LT,
      "( n1 -- flag )  Less than zero\n"
      "Returns *-1* if *n1* less than zero, *0* otherwise."),
    _FF_W("and", FF_OP_AND,
      "( n1 n2 -- n3 )  Bitwise and\n"
      "Stores the bitwise and of *n1* and *n2* on the stack."),
    _FF_W("or", FF_OP_OR,
      "( n1 n2 -- n3 )  Bitwise or\n"
      "Stores the bitwise or of *n1* and *n2* on the stack."),
    _FF_W("xor", FF_OP_XOR,
      "( n1 n2 -- n3 )  Bitwise exclusive or\n"
      "Stores the bitwise exclusive or of *n1* and *n2* on the stack."),
    _FF_W("not", FF_OP_NOT,
      "( n1 -- n2 )  Logical not\n"
      "Inverts the bits in the value on the top of the stack.\n"
      "This performs logical negation for truth values of\n"
      "*-1* (true) and *0* (false)."),
    _FF_W("shift", FF_OP_SHIFT,
      "( n1 n2 -- n3 )  Shift n1 by n2 bits\n"
      "The value *n1* is logically shifted the number of bits\n"
      "specified by *n2*, left if *n2* is positive and right\n"
      "if *n2* is negative. Zero bits are shifted into vacated bits."),
    _FF_W("1+", FF_OP_INC,
      "( n1 -- n2 )  Add one\n"
      "Adds one to top of stack."),
    _FF_W("1-", FF_OP_DEC,
      "( n1 -- n2 )  Subtract one\n"
      "Subtracts one from top of stack."),
    _FF_W("2+", FF_OP_INC2,
      "( n1 -- n2 )  Add two\n"
      "Adds two to top of stack."),
    _FF_W("2-", FF_OP_DEC2,
      "( n1 -- n2 )  Subtract two\n"
      "Subtracts two from top of stack."),
    _FF_W("2*", FF_OP_MUL2,
      "( n1 -- n2 )  Times two\n"
      "Multiplies the top of stack by two."),
    _FF_W("2/", FF_OP_DIV2,
      "( n1 -- n2 )  Divide by two\n"
      "Divides top of stack by two."),
    _FF_W("base", FF_OP_SET_BASE,
      "( n -- )  Numeric base\n"
      "Sets numeric base to *n*. Only *10* and *16* are supported at the moment."),
    FF_WEND
};

