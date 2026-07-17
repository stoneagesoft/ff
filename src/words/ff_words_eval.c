/*
 * ff --- eval word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Eval words
 * =================================================================== */

const ff_word_def_t FF_EVAL_WORDS[] =
{
    _FF_W("evaluate", FF_OP_EVALUATE,
      "( s -- stat )  Evaluate string\n"
      "Gets string from stack and leaves its evaluation status."),
    _FF_W("load", FF_OP_LOAD,
      "( path -- stat )  Load file\n"
      "The source program is loaded from the file as if its text\n"
      "appeared at the current character position in the input stream.\n"
      "The status resulting from the evaluation is left on the stack,\n"
      "zero if normal, negative in case of error.\n"
      "\n"
      "See also: **evaluate**"),
    _FF_W("parse-word", FF_OP_PARSE_WORD,
      "( -- s )  Parse next token\n"
      "Parses the next whitespace-delimited token from the input stream\n"
      "and leaves it as a NUL-terminated string. At end of input the\n"
      "string is empty (`strlen` 0). Unlike `'`, the token is returned as\n"
      "text and is not looked up in the dictionary — this is the primitive\n"
      "for writing parsing words in Forth. Combine with **evaluate** to\n"
      "define new notation without extending the engine in C.\n"
      "\n"
      "See also: **parse**, **evaluate**"),
    _FF_W("parse", FF_OP_PARSE,
      "( char -- s )  Parse to delimiter\n"
      "Parses text from the current input position up to and including the\n"
      "next occurrence of *char* (given as its character code), and leaves\n"
      "the text before it as a NUL-terminated string. Leading delimiters\n"
      "are not skipped, so successive calls can return empty strings.\n"
      "\n"
      "    41 parse group) type   \\ 41 is ')' — prints 'group'\n"
      "\n"
      "See also: **parse-word**"),
    FF_WEND
};

