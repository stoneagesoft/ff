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
    _FF_W(".", FF_OP_DOT,
      "( n -- )  Print top of stack\n"
      "Prints the number on the top of the stack."),
    _FF_W("?", FF_OP_QUESTION,
      "( addr -- )  Print indirect\n"
      "Prints the value at the address at the top of the stack."),
    _FF_W("cr", FF_OP_CR,
      "( -- )  Carriage return\n"
      "The standard output stream is advanced to the first character of the next line."),
    _FF_W("emit", FF_OP_EMIT,
      "( c -- )  Emit character\n"
      "The character *c* is printed on standard output."),
    _FF_W("type", FF_OP_TYPE,
      "( s -- )  Print string\n"
      "The string at address *s* is printed on standard output."),
    _FF_W(".s", FF_OP_DOT_S,
      "( -- )  Print stack\n"
      "Prints entire contents of stack."),
    _FF_WI(".\"", FF_OP_DOTQUOTE,
      "s ( -- )  Print immediate string\n"
      "Prints the string literal s that follows in line."),
    _FF_WI(".(", FF_OP_DOT_PAREN,
      "s ( -- )  Print constant string\n"
      "Immediately prints the string s that follows in the input stream."),
    FF_WEND
};

