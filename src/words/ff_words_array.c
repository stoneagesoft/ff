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
    _FF_W("array", FF_OP_ARRAY,
      "w ( n -- )  Declare array\n"
      "Declares an array *w* of *n* elements. Element size is stack item size.\n"
      "When *w* is executed, it gets index from the stack's top and puts\n"
      "address of the element at that index on the stack."),
    FF_WEND
};
