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
    FF_WEND
};

