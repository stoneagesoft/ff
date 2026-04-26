/*
 * ff --- comp word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Compilation words
 * =================================================================== */

const ff_word_def_t FF_COMP_WORDS[] =
{
    _FF_W(":", FF_OP_COLON,
      "w ( -- )  Begin definition\n"
      "Begins compilation of a word named *w*."),
    _FF_WI(";", FF_OP_SEMICOLON,
      "( -- )  End definition\n"
      "Ends compilation of word."),
    _FF_W("immediate", FF_OP_IMMEDIATE,
      "( -- )  Mark immediate\n"
      "The most recently defined word is marked for immediate execution;\n"
      "it will be executed even if entered in compile state."),
    _FF_WI("[", FF_OP_LBRACKET,
      "( -- )  Set interpretive state\n"
      "Within a compilation, returns to the interpretive state."),
    _FF_W("]", FF_OP_RBRACKET,
      "( -- )  End interpretive state\n"
      "Restore compile state after temporary interpretive state."),
    _FF_W("'", FF_OP_TICK,
      "w ( -- cfa )  Obtain compilation address\n"
      "Places the compilation address of the following word *w* on the stack."),
    _FF_WI("[']", FF_OP_BRACKET_TICK,
      "w ( -- cfa )  Push next word\n"
      "Places the compile address of the following word *w* in a definition onto the stack."),
    _FF_W("execute", FF_OP_EXECUTE,
      "( cfa -- )  Execute word\n"
      "Executes the word with compile address *cfa*."),
    _FF_W("state", FF_OP_STATE,
      "( -- addr )  System state variable\n"
      "The address of the system state variable is pushed on the stack.\n"
      "The state is zero if interpreting, nonzero if compiling."),
    _FF_WI("[compile]", FF_OP_BRACKET_COMPILE,
      "w ( -- )  Compile immediate word\n"
      "Compiles the address of word *w*, even if *w* is marked as *immediate*."),
    _FF_WI("literal", FF_OP_LITERAL,
      "( n -- )  Compile literal\n"
      "Compiles the value on the top of the stack into the current definition.\n"
      "When the definition is executed, that value will be pushed onto\n"
      "the top of the stack."),
    _FF_W("compile", FF_OP_COMPILE,
      "w ( -- )  Compile word\n"
      "Adds the compile address of the word *w* that follows\n"
      "in line to the definition currently being compiled."),
    _FF_W("does>", FF_OP_DOES,
      "( -- )  Run-time action\n"
      "Sets the run-time action of a word created by the last\n"
      "**create** to the code that follows. When the word is executed,\n"
      "its body address is pushed on the stack, then the code\n"
      "that follows the **does>** will be executed."),
    FF_WEND
};

