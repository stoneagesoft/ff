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
    _FF_W("(strlit)", FF_OP_STRLIT,
      "( -- s )  String literal\n"
      "Pushes the address of the string literal that follows in line onto the stack."),
    _FF_W("string", FF_OP_STRING,
      "w ( size -- )  Declare string\n"
      "Declares a string named *w* of a maximum of *size - 1* characters."),
    _FF_W("s!", FF_OP_S_STORE,
      "( s1 s2 -- )  Store string\n"
      "The string at address *s1* is copied into the string at *s2*."),
    _FF_W("s+", FF_OP_S_CAT,
      "( s1 s2 -- )  String concatenate\n"
      "The string at address *s1* is concatenated to the string at address *s2*."),
    _FF_W("strlen", FF_OP_STRLEN,
      "( s -- n )  String length\n"
      "The length of string *s* is placed on the top of the stack."),
    _FF_W("strcmp", FF_OP_STRCMP,
      "( s1 s2 -- n )  String compare\n"
      "The string at address *s1* is compared to the string at address *s2*.\n"
      "Returns an integer less than, equal to, or greater than zero if *s1*\n"
      "is found, respectively, to be less than, to match,\n"
      "or be greater than *s2*."),
    FF_WEND
};

