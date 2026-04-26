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
    _FF_W("here", FF_OP_HERE,
      "( -- addr )  Heap address\n"
      "The current heap allocation address is placed on the top of the stack."),
    _FF_W("!", FF_OP_STORE,
      "( n addr -- )  Store into address\n"
      "Stores the value *n* into the address *addr*."),
    _FF_W("@", FF_OP_FETCH,
      "( addr -- n )  Load\n"
      "Loads the value at *addr* and leaves it at the top of the stack."),
    _FF_W("+!", FF_OP_PLUS_STORE,
      "( n addr -- )  Add indirect\n"
      "Adds *n* to the word at address *addr*."),
    _FF_W("allot", FF_OP_ALLOT,
      "( n -- )  Allocate heap\n"
      "Allocates *n* bytes of heap space."),
    _FF_W(",", FF_OP_COMMA,
      "( n -- )  Store in heap\n"
      "Reserves one cell of heap space, initializing it to *n*."),
    _FF_W("c!", FF_OP_C_STORE,
      "( n addr -- )  Store byte\n"
      "The 8 bit value *n* is stored in the byte at address *addr*."),
    _FF_W("c@", FF_OP_C_FETCH,
      "( addr -- n )  Load byte\n"
      "The byte at address *addr* is placed on the top of the stack."),
    _FF_W("c,", FF_OP_C_COMMA,
      "( n -- )  Compile byte\n"
      "The 8 bit value *n* is stored in the next free byte of the\n"
      "heap and the heap pointer is incremented by one."),
    _FF_W("c=", FF_OP_C_ALIGN,
      "( -- )  Align heap\n"
      "The heap allocation pointer is adjusted to the next\n"
      "eight byte boundary. This must be done following a sequence of *c,*"),
    FF_WEND
};

