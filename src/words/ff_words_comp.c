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
      "Begins compilation of a word named *w*. The word can call itself by\n"
      "name (see also **recurse**). If an error ends the input before the\n"
      "matching **;**, the definition is discarded: *w* is not left in the\n"
      "dictionary half-compiled. Definitions don't nest."),
    _FF_WI(";", FF_OP_SEMICOLON,
      "( -- )  End definition\n"
      "Ends compilation of word. Every **if**, **begin**, **do** and **{**\n"
      "opened in the definition must be closed by then."),
    _FF_WI("{", FF_OP_LBRACE,
      "( a b -- c )  Open a stack scope\n"
      "Opens a scope over the data stack. The scope must be followed by a\n"
      "signature `( a b -- c )`, where the names left of `--` bind the\n"
      "caller's topmost cells and the names right of `--` declare how many\n"
      "cells the scope leaves.\n"
      "\n"
      "Code inside the scope sees an **empty** stack: it can push and pop\n"
      "freely, but reaching below what it pushed raises a stack underflow\n"
      "rather than consuming the caller's cells. The declared inputs are\n"
      "readable only by name.\n"
      "\n"
      "`...` opts out of a check: `( a b -- ... )` leaves an unchecked\n"
      "number of cells, and `( ... -- ... )` inherits the enclosing barrier\n"
      "instead of installing a new one. `...` on the input side cannot be\n"
      "combined with named inputs, and forces `...` on the output side.\n"
      "\n"
      "    : dist { ( x1 y1 x2 y2 -- d )\n"
      "        x2 x1 f- dup f*  y2 y1 f- dup f*  f+ sqrt } ;\n"
      "\n"
      "See also: **}**"),
    _FF_WI("}", FF_OP_RBRACE,
      "( -- )  Close a stack scope\n"
      "Closes the scope opened by the matching `{`: checks that the scope\n"
      "left exactly the number of cells its signature declared, drops the\n"
      "named inputs, and leaves the outputs in their place.\n"
      "\n"
      "See also: **{**"),
    _FF_W("immediate", FF_OP_IMMEDIATE,
      "( -- )  Mark immediate\n"
      "The most recently defined word is marked for immediate execution;\n"
      "it will be executed even if entered in compile state."),
    _FF_WI("[", FF_OP_LBRACKET,
      "( -- )  Set interpretive state\n"
      "Within a compilation, returns to the interpretive state."),
    _FF_W("]", FF_OP_RBRACKET,
      "( -- )  End interpretive state\n"
      "Resume compiling the definition that **[** left."),
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
      "( -- flag )  Compilation state\n"
      "Pushes true (-1) while compiling and false (0) while interpreting.\n"
      "It pushes the flag itself, not the address of a variable as in\n"
      "ANS Forth: write `state`, not `state @`."),
    _FF_WI("[compile]", FF_OP_BRACKET_COMPILE,
      "w ( -- )  Compile immediate word\n"
      "Compiles the address of word *w*, even if *w* is marked as *immediate*."),
    _FF_WI("literal", FF_OP_LITERAL,
      "( n -- )  Compile literal\n"
      "Compiles the value on the top of the stack into the current definition.\n"
      "When the definition is executed, that value will be pushed onto\n"
      "the top of the stack."),
    _FF_WI("postpone", FF_OP_POSTPONE,
      "w ( -- )  Postpone compilation\n"
      "Parses the next word *w* and appends its compilation semantics to\n"
      "the current definition. If *w* is immediate, a call to *w* is\n"
      "compiled so it runs when the new definition runs; if *w* is not\n"
      "immediate, code is compiled that will itself compile a call to *w*\n"
      "when the new definition runs. This is the standard way to build a\n"
      "word that lays down control-flow words such as **if** or **then**.\n"
      "\n"
      "Supersedes the older **compile** / **[compile]** pair."),
    _FF_WI("compile", FF_OP_COMPILE,
      "w ( -- )  Compile word\n"
      "Used in an immediate word: when that word runs, it adds a call to\n"
      "the word *w* that follows in line to the definition then being\n"
      "compiled. Like **postpone**, but *w* is compiled even if it is\n"
      "immediate."),
    _FF_WI("recurse", FF_OP_RECURSE,
      "( -- )  Recursive call\n"
      "Compiles a call to the definition being compiled. A definition can\n"
      "also call itself by its name; **recurse** is the standard spelling."),
    _FF_W("does>", FF_OP_DOES,
      "( -- )  Run-time action\n"
      "Sets the run-time action of a word created by the last\n"
      "**create** to the code that follows. When the word is executed,\n"
      "its body address is pushed on the stack, then the code\n"
      "that follows the **does>** will be executed."),
    FF_WEND
};

