/*
 * ff --- var word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Variable / constant words
 * =================================================================== */

const ff_word_def_t FF_VAR_WORDS[] =
{
    _FF_W("create", FF_OP_CREATE,
      "( -- )  Create object\n"
      "Create an object, given the name which appears next in the input stream,\n"
      "with a default action of pushing the parameter field address of the object\n"
      "when executed. No storage is allocated; normally the parameter field will\n"
      "be allocated and initialized by the defining word code that follows the **create**."),
    _FF_W("forget", FF_OP_FORGET,
      "w ( -- )  Forget word\n"
      "The most recent definition of word *w* is deleted, along with\n"
      "all words declared more recently than the named word."),
    _FF_W("variable", FF_OP_VARIABLE,
      "w ( -- )  Declare variable\n"
      "A variable named *w* is declared and its value is set to zero.\n"
      "When *w* is executed, its address will be placed on the stack."),
    _FF_W("constant", FF_OP_CONSTANT,
      "w ( n -- )  Declare constant\n"
      "Declares a constant named *w*. When *w* is executed,\n"
      "the value *n* will be left on the stack."),
    _FF_W("defer", FF_OP_DEFER,
      "w ( -- )  Declare a deferred word\n"
      "A deferred word named *w* is created with no action. Executing *w*\n"
      "before any action has been assigned raises FF_ERR_BAD_PTR.\n"
      "Use **is** to assign the action: `' some-word is w`."),
    _FF_W("is", FF_OP_IS,
      "w ( xt -- )  Set deferred word's action\n"
      "Pops *xt* from the data stack and stores it as the action of the\n"
      "deferred word named *w* (which must have been created with **defer**).\n"
      "After this, executing *w* runs the word identified by *xt*."),
    FF_WEND
};

