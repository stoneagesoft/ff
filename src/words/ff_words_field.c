/*
 * ff --- field word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Field / introspection words
 * =================================================================== */

const ff_word_def_t FF_FIELD_WORDS[] =
{
    _FF_W("find", FF_OP_FIND,
      "( s -- word flag )  Look up word\n"
      "The word with name given by the string *s* is looked up\n"
      "in the dictionary. If a definition is not found,\n"
      "word will be left as the address of the string and *flag*\n"
      "will be set to zero."),
    _FF_W(">name", FF_OP_TO_NAME,
      "( cfa -- nfa )  Name address\n"
      "Given the compile address of a word, return its name pointer field address."),
    _FF_W(">body", FF_OP_TO_BODY,
      "( cfa -- pfa )  Body address\n"
      "Given the compile address of a word, return its body (parameter) address."),
    FF_WEND
};

