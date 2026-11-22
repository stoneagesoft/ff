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
    _FF_W("create", FF_OP_CREATE, "( -- )",
      FF_TR_EN("Create object\n"
               "Create an object, given the name which appears next in the input stream,\n"
               "with a default action of pushing the parameter field address of the object\n"
               "when executed. No storage is allocated; normally the parameter field will\n"
               "be allocated and initialized by the defining word code that follows the **create**."),
      FF_TR_RU("Создать объект\n"
               "Создаёт объект с именем, идущим следующим во входном потоке; по умолчанию\n"
               "при выполнении он кладёт на стек адрес своего поля параметров.\n"
               "Память не выделяется: обычно поле параметров выделяет и заполняет код\n"
               "определяющего слова, следующий за **create**.")),
    _FF_W("forget", FF_OP_FORGET, "w ( -- )",
      FF_TR_EN("Forget word\n"
               "The most recent definition of word *w* is deleted, along with\n"
               "all words declared more recently than the named word. Refused while\n"
               "a definition is being compiled, or if one of those words is running\n"
               "or was registered by the host."),
      FF_TR_RU("Забыть слово\n"
               "Удаляет последнее определение слова *w* вместе со всеми словами,\n"
               "объявленными после него. Отказывает, пока компилируется определение\n"
               "или если одно из этих слов выполняется либо зарегистрировано хостом.")),
    _FF_W("variable", FF_OP_VARIABLE, "w ( -- )",
      FF_TR_EN("Declare variable\n"
               "A variable named *w* is declared and its value is set to zero.\n"
               "When *w* is executed, its address will be placed on the stack."),
      FF_TR_RU("Объявить переменную\n"
               "Объявляет переменную с именем *w* и обнуляет её значение.\n"
               "При выполнении *w* на стек кладётся её адрес.")),
    _FF_W("constant", FF_OP_CONSTANT, "w ( n -- )",
      FF_TR_EN("Declare constant\n"
               "Declares a constant named *w*. When *w* is executed,\n"
               "the value *n* will be left on the stack."),
      FF_TR_RU("Объявить константу\n"
               "Объявляет константу с именем *w*. При выполнении *w*\n"
               "на стеке остаётся значение *n*.")),
    _FF_W("defer", FF_OP_DEFER, "w ( -- )",
      FF_TR_EN("Declare a deferred word\n"
               "A deferred word named *w* is created with no action. Executing *w*\n"
               "before any action has been assigned raises FF_ERR_BAD_PTR.\n"
               "Use **is** to assign the action: `' some-word is w`."),
      FF_TR_RU("Объявить отложенное слово\n"
               "Создаёт отложенное слово с именем *w* без действия. Выполнение *w*\n"
               "до назначения действия вызывает FF_ERR_BAD_PTR.\n"
               "Действие назначается словом **is**: `' some-word is w`.")),
    _FF_WI("is", FF_OP_IS, "w ( xt -- )",
      FF_TR_EN("Set deferred word's action\n"
               "Pops *xt* from the data stack and stores it as the action of the\n"
               "deferred word named *w* (which must have been created with **defer**).\n"
               "After this, executing *w* runs the word identified by *xt*. In a\n"
               "definition, *w* is read when the definition is compiled, and the\n"
               "definition sets the action when it runs: `: use-ten ['] ten is hook ;`."),
      FF_TR_RU("Назначить действие отложенного слова\n"
               "Снимает *xt* со стека данных и сохраняет его как действие отложенного\n"
               "слова с именем *w* (созданного словом **defer**). После этого\n"
               "выполнение *w* запускает слово, заданное *xt*. В определении *w*\n"
               "читается при компиляции определения, а действие назначается, когда\n"
               "определение выполняется: `: use-ten ['] ten is hook ;`.")),
    FF_WEND
};

