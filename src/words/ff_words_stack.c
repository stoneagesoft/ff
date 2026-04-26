/*
 * ff --- stack word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Stack words
 * =================================================================== */

const ff_word_def_t FF_STACK_WORDS[] =
{
    _FF_W("(lit)", FF_OP_LIT,
      "( -- n )  Push literal\n"
      "Pushes the integer literal that follows in line onto the top of the stack."),
    _FF_W("depth", FF_OP_DEPTH,
      "( -- n )  Stack depth\n"
      "Returns the number of items on the stack before **depth** was executed."),
    _FF_W("clear", FF_OP_CLEAR,
      "( ... -- )  Clear stack\n"
      "All items on the stack are discarded."),
    _FF_W("dup", FF_OP_DUP,
      "( n -- n n )  Duplicate\n"
      "Duplicates the value at the top of the stack."),
    _FF_W("drop", FF_OP_DROP,
      "( n -- )  Discard top of stack\n"
      "Discards the value at the top of the stack."),
    _FF_W("swap", FF_OP_SWAP,
      "( n1 n2 -- n2 n1 )  Swap top two items\n"
      "The top two stack items are interchanged."),
    _FF_W("over", FF_OP_OVER,
      "( n1 n2 -- n1 n2 n1 )  Duplicate second item\n"
      "The second item on the stack is copied to the top."),
    _FF_W("pick", FF_OP_PICK,
      "( ... n2 n1 n0 index -- ... n0 nindex )  Pick item from stack\n"
      "The *index*-th stack item is copied to the top of the stack.\n"
      "The top of stack has index *0*, the second item index *1*, and so on."),
    _FF_W("rot", FF_OP_ROT,
      "( n1 n2 n3 -- n2 n3 n1 )  Rotate 3 items\n"
      "The third item on the stack is placed on the top of the stack\n"
      "and the second and first items are moved down."),
    _FF_W("-rot", FF_OP_NROT,
      "( n1 n2 n3 -- n3 n1 n2 )  Reverse rotate\n"
      "Moves the top of stack to the third item, moving the third\n"
      "and second items up."),
    _FF_W("roll", FF_OP_ROLL,
      "( ... n2 n1 n0 index -- ... n0 nindex )  Rotate index-th item to top\n"
      "The stack item selected by *index*, with *0* designating the top of stack,\n"
      "*1* the second item, and so on, is moved to the top of the stack.\n"
      "The intervening stack items are moved down one item."),
    _FF_W(">r", FF_OP_TO_R,
      "( n -- )  To return stack\n"
      "Removes the top item from the stack and pushes it onto the return stack."),
    _FF_W("r>", FF_OP_FROM_R,
      "( -- n )  From return stack\n"
      "The top value is removed from the return stack and pushed onto the stack."),
    _FF_W("r@", FF_OP_FETCH_R,
      "( -- n )  Fetch return stack\n"
      "The top value on the return stack is pushed onto the stack.\n"
      "The value is not removed from the return stack."),
    FF_WEND
};

