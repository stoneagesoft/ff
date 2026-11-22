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
    _FF_W("find", FF_OP_FIND, "( s -- word flag )",
      FF_TR_EN("Look up word\n"
               "The word with name given by the string *s* is looked up\n"
               "in the dictionary. If a definition is not found,\n"
               "word will be left as the address of the string and *flag*\n"
               "will be set to zero."),
      FF_TR_RU("Найти слово\n"
               "Слово с именем из строки *s* ищется в словаре.\n"
               "Если определение не найдено, на месте слова остаётся адрес строки,\n"
               "а *flag* равен нулю.")),
    _FF_W(">name", FF_OP_TO_NAME, "( cfa -- nfa )",
      FF_TR_EN("Name address\n"
               "Given the compile address of a word, return its name pointer field address."),
      FF_TR_RU("Адрес имени\n"
               "По адресу компиляции слова возвращает адрес поля указателя на его имя.")),
    _FF_W(">body", FF_OP_TO_BODY, "( cfa -- pfa )",
      FF_TR_EN("Body address\n"
               "Given the compile address of a word, return its body (parameter) address."),
      FF_TR_RU("Адрес тела\n"
               "По адресу компиляции слова возвращает адрес его тела (поля параметров).")),
    FF_WEND
};

