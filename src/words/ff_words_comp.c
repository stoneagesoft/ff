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
    _FF_W(":", FF_OP_COLON, "w ( -- )",
      FF_TR_EN("Begin definition\n"
               "Begins compilation of a word named *w*. The word can call itself by\n"
               "name (see also **recurse**). If an error ends the input before the\n"
               "matching **;**, the definition is discarded: *w* is not left in the\n"
               "dictionary half-compiled. Definitions don't nest."),
      FF_TR_RU("Начать определение\n"
               "Начинает компиляцию слова с именем *w*. Слово может вызывать себя\n"
               "по имени (см. также **recurse**). Если ошибка обрывает ввод до парного\n"
               "**;**, определение отбрасывается: недокомпилированное *w* не остаётся\n"
               "в словаре. Определения не вкладываются друг в друга.")),
    _FF_WI(";", FF_OP_SEMICOLON, "( -- )",
      FF_TR_EN("End definition\n"
               "Ends compilation of word. Every **if**, **begin**, **do** and **{**\n"
               "opened in the definition must be closed by then."),
      FF_TR_RU("Закончить определение\n"
               "Завершает компиляцию слова. К этому моменту каждое **if**, **begin**,\n"
               "**do** и **{**, открытое в определении, должно быть закрыто.")),
    _FF_WI("{", FF_OP_LBRACE, "( a b -- c )",
      FF_TR_EN("Open a stack scope\n"
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
      FF_TR_RU("Открыть область стека\n"
               "Открывает область над стеком данных. За ней должна идти сигнатура\n"
               "`( a b -- c )`: имена слева от `--` связываются с верхними ячейками\n"
               "вызывающего кода, а имена справа от `--` объявляют, сколько ячеек\n"
               "область оставит.\n"
               "\n"
               "Код внутри области видит **пустой** стек: он может свободно класть\n"
               "и снимать значения, но попытка достать что-то ниже положенного им\n"
               "вызывает исчерпание стека, а не забирает ячейки вызывающего кода.\n"
               "Объявленные входы доступны только по имени.\n"
               "\n"
               "`...` отключает проверку: `( a b -- ... )` оставляет непроверяемое\n"
               "число ячеек, а `( ... -- ... )` наследует объемлющий барьер вместо\n"
               "установки нового. `...` на стороне входов нельзя сочетать\n"
               "с именованными входами, и оно требует `...` на стороне выходов.\n"
               "\n"
               "    : dist { ( x1 y1 x2 y2 -- d )\n"
               "        x2 x1 f- dup f*  y2 y1 f- dup f*  f+ sqrt } ;\n"
               "\n"
               "См. также: **}**")),
    _FF_WI("}", FF_OP_RBRACE, "( -- )",
      FF_TR_EN("Close a stack scope\n"
               "Closes the scope opened by the matching `{`: checks that the scope\n"
               "left exactly the number of cells its signature declared, drops the\n"
               "named inputs, and leaves the outputs in their place.\n"
               "\n"
               "See also: **{**"),
      FF_TR_RU("Закрыть область стека\n"
               "Закрывает область, открытую парной `{`: проверяет, что область оставила\n"
               "ровно столько ячеек, сколько объявила её сигнатура, снимает именованные\n"
               "входы и оставляет на их месте выходы.\n"
               "\n"
               "См. также: **{**")),
    _FF_W("immediate", FF_OP_IMMEDIATE, "( -- )",
      FF_TR_EN("Mark immediate\n"
               "The most recently defined word is marked for immediate execution;\n"
               "it will be executed even if entered in compile state."),
      FF_TR_RU("Пометить как немедленное\n"
               "Последнее определённое слово помечается для немедленного исполнения:\n"
               "оно будет выполняться, даже если встретится в режиме компиляции.")),
    _FF_WI("[", FF_OP_LBRACKET, "( -- )",
      FF_TR_EN("Set interpretive state\n"
               "Within a compilation, returns to the interpretive state."),
      FF_TR_RU("Режим интерпретации\n"
               "Внутри компиляции возвращает в режим интерпретации.")),
    _FF_W("]", FF_OP_RBRACKET, "( -- )",
      FF_TR_EN("End interpretive state\n"
               "Resume compiling the definition that **[** left."),
      FF_TR_RU("Закончить режим интерпретации\n"
               "Продолжает компиляцию определения, прерванную **[**.")),
    _FF_W("'", FF_OP_TICK, "w ( -- cfa )",
      FF_TR_EN("Obtain compilation address\n"
               "Places the compilation address of the following word *w* on the stack."),
      FF_TR_RU("Получить адрес компиляции\n"
               "Кладёт на стек адрес компиляции следующего слова *w*.")),
    _FF_WI("[']", FF_OP_BRACKET_TICK, "w ( -- cfa )",
      FF_TR_EN("Push next word\n"
               "Places the compile address of the following word *w* in a definition onto the stack."),
      FF_TR_RU("Положить следующее слово\n"
               "В определении кладёт на стек адрес компиляции следующего слова *w*.")),
    _FF_W("execute", FF_OP_EXECUTE, "( cfa -- )",
      FF_TR_EN("Execute word\n"
               "Executes the word with compile address *cfa*."),
      FF_TR_RU("Выполнить слово\n"
               "Выполняет слово с адресом компиляции *cfa*.")),
    _FF_W("state", FF_OP_STATE, "( -- flag )",
      FF_TR_EN("Compilation state\n"
               "Pushes true (-1) while compiling and false (0) while interpreting.\n"
               "It pushes the flag itself, not the address of a variable as in\n"
               "ANS Forth: write `state`, not `state @`."),
      FF_TR_RU("Состояние компиляции\n"
               "Кладёт истину (-1) во время компиляции и ложь (0) во время интерпретации.\n"
               "Кладётся сам флаг, а не адрес переменной, как в ANS Forth:\n"
               "пишите `state`, а не `state @`.")),
    _FF_WI("[compile]", FF_OP_BRACKET_COMPILE, "w ( -- )",
      FF_TR_EN("Compile immediate word\n"
               "Compiles the address of word *w*, even if *w* is marked as *immediate*."),
      FF_TR_RU("Скомпилировать немедленное слово\n"
               "Компилирует адрес слова *w*, даже если *w* помечено как *immediate*.")),
    _FF_WI("literal", FF_OP_LITERAL, "( n -- )",
      FF_TR_EN("Compile literal\n"
               "Compiles the value on the top of the stack into the current definition.\n"
               "When the definition is executed, that value will be pushed onto\n"
               "the top of the stack."),
      FF_TR_RU("Скомпилировать литерал\n"
               "Компилирует значение с вершины стека в текущее определение.\n"
               "Когда определение выполняется, это значение кладётся\n"
               "на вершину стека.")),
    _FF_WI("postpone", FF_OP_POSTPONE, "w ( -- )",
      FF_TR_EN("Postpone compilation\n"
               "Parses the next word *w* and appends its compilation semantics to\n"
               "the current definition. If *w* is immediate, a call to *w* is\n"
               "compiled so it runs when the new definition runs; if *w* is not\n"
               "immediate, code is compiled that will itself compile a call to *w*\n"
               "when the new definition runs. This is the standard way to build a\n"
               "word that lays down control-flow words such as **if** or **then**.\n"
               "\n"
               "Supersedes the older **compile** / **[compile]** pair."),
      FF_TR_RU("Отложить компиляцию\n"
               "Читает следующее слово *w* и добавляет его семантику компиляции\n"
               "в текущее определение. Если *w* немедленное, компилируется вызов *w*,\n"
               "который выполнится вместе с новым определением; если *w* не немедленное,\n"
               "компилируется код, который сам скомпилирует вызов *w*, когда выполнится\n"
               "новое определение. Это стандартный способ написать слово, которое\n"
               "закладывает слова управления, такие как **if** или **then**.\n"
               "\n"
               "Заменяет старую пару **compile** / **[compile]**.")),
    _FF_WI("compile", FF_OP_COMPILE, "w ( -- )",
      FF_TR_EN("Compile word\n"
               "Used in an immediate word: when that word runs, it adds a call to\n"
               "the word *w* that follows in line to the definition then being\n"
               "compiled. Like **postpone**, but *w* is compiled even if it is\n"
               "immediate."),
      FF_TR_RU("Скомпилировать слово\n"
               "Используется в немедленном слове: когда это слово выполняется, оно\n"
               "добавляет в компилируемое в этот момент определение вызов слова *w*,\n"
               "записанного сразу за ним. Похоже на **postpone**, но *w* компилируется,\n"
               "даже если оно немедленное.")),
    _FF_WI("recurse", FF_OP_RECURSE, "( -- )",
      FF_TR_EN("Recursive call\n"
               "Compiles a call to the definition being compiled. A definition can\n"
               "also call itself by its name; **recurse** is the standard spelling."),
      FF_TR_RU("Рекурсивный вызов\n"
               "Компилирует вызов компилируемого определения. Определение может\n"
               "вызывать себя и по имени; **recurse** — стандартная запись.")),
    _FF_W("does>", FF_OP_DOES, "( -- )",
      FF_TR_EN("Run-time action\n"
               "Sets the run-time action of a word created by the last\n"
               "**create** to the code that follows. When the word is executed,\n"
               "its body address is pushed on the stack, then the code\n"
               "that follows the **does>** will be executed."),
      FF_TR_RU("Действие во время выполнения\n"
               "Задаёт действие во время выполнения для слова, созданного последним\n"
               "**create**: следующий за **does>** код. Когда слово выполняется,\n"
               "на стек кладётся адрес его тела, а затем выполняется код,\n"
               "следующий за **does>**.")),
    FF_WEND
};

