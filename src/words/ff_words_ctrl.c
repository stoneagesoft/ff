/*
 * ff --- ctrl word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * Control flow words
 * =================================================================== */

const ff_word_def_t FF_CTRL_WORDS[] =
{
    _FF_W("exit", FF_OP_EXIT, "( -- )",
      FF_TR_EN("Exit definition\n"
               "Exit from the current definition immediately. Inside a **do-loop**\n"
               "the loop's parameters are discarded first, and inside a **{** scope\n"
               "the scope is closed first, as **}** would close it: the cells left\n"
               "on its stack must be the outputs it declares."),
      FF_TR_RU("Выйти из определения\n"
               "Немедленно выходит из текущего определения. Внутри **do-loop**\n"
               "сначала отбрасываются параметры цикла, а внутри области **{**\n"
               "сначала закрывается область, как её закрыло бы **}**: оставшиеся\n"
               "на её стеке ячейки должны быть объявленными в ней выходами.")),
    _FF_W("branch", FF_OP_BRANCH, "( -- )",
      FF_TR_EN("Branch\n"
               "Jump to the address that follows in line."),
      FF_TR_RU("Переход\n"
               "Переходит по адресу, записанному в коде сразу за словом.")),
    _FF_W("?branch", FF_OP_QBRANCH, "( flag -- )",
      FF_TR_EN("Conditional branch\n"
               "If the top of stack is zero, jump to the address which\n"
               "follows in line. Otherwise skip the address and continue execution."),
      FF_TR_RU("Условный переход\n"
               "Если на вершине стека ноль, переходит по адресу, записанному\n"
               "в коде сразу за словом. Иначе пропускает адрес и продолжает выполнение.")),
    _FF_W("?dup", FF_OP_QDUP, "( n -- 0 / n n )",
      FF_TR_EN("Conditional duplicate\n"
               "If top of stack is nonzero, duplicate it.\n"
               "Otherwise leave zero on top of stack."),
      FF_TR_RU("Условное дублирование\n"
               "Если вершина стека не равна нулю, дублирует её.\n"
               "Иначе оставляет на вершине стека ноль.")),
    _FF_WI("if", FF_OP_IF, "( flag -- )",
      FF_TR_EN("Conditional statement\n"
               "If *flag* is nonzero, the following statements are executed.\n"
               "Otherwise, execution resumes after the matching *else* clause,\n"
               "if any, or after the matching **then**."),
      FF_TR_RU("Условный оператор\n"
               "Если *flag* не равен нулю, выполняются следующие за ним операторы.\n"
               "Иначе выполнение продолжается после парной ветви *else*,\n"
               "если она есть, или после парного **then**.")),
    _FF_WI("else", FF_OP_ELSE, "( -- )",
      FF_TR_EN("Else\n"
               "Used in an **if-else-then** sequence, delimits the code\n"
               "to be executed if the if-condition was false."),
      FF_TR_RU("Иначе\n"
               "В конструкции **if-else-then** отделяет код, который выполняется,\n"
               "если условие if ложно.")),
    _FF_WI("then", FF_OP_THEN, "( -- )",
      FF_TR_EN("End if\n"
               "Used in an **if-else-then** sequence, marks the end of\n"
               "the conditional statement."),
      FF_TR_RU("Конец условия\n"
               "В конструкции **if-else-then** отмечает конец\n"
               "условного оператора.")),
    _FF_WI("begin", FF_OP_BEGIN, "( -- )",
      FF_TR_EN("Begin loop\n"
               "Begins an indefinite loop. The end of the loop\n"
               "is marked by the matching **again**, **repeat**, or **until**."),
      FF_TR_RU("Начало цикла\n"
               "Начинает цикл с заранее неизвестным числом повторений. Конец цикла\n"
               "отмечает парное **again**, **repeat** или **until**.")),
    _FF_WI("until", FF_OP_UNTIL, "( flag -- )",
      FF_TR_EN("End begin-until loop\n"
               "If *flag* is zero, the loop continues execution at\n"
               "the word following the matching **begin**. If *flag* is nonzero,\n"
               "the loop is exited and the word following the **until** is executed."),
      FF_TR_RU("Конец цикла begin-until\n"
               "Если *flag* равен нулю, цикл продолжается со слова,\n"
               "следующего за парным **begin**. Если *flag* не равен нулю,\n"
               "цикл завершается и выполняется слово, следующее за **until**.")),
    _FF_WI("again", FF_OP_AGAIN, "( -- )",
      FF_TR_EN("Indefinite loop\n"
               "Marks the end of an indefinite loop opened by the matching **begin**."),
      FF_TR_RU("Бесконечный цикл\n"
               "Отмечает конец бесконечного цикла, открытого парным **begin**.")),
    _FF_WI("while", FF_OP_WHILE, "( flag -- )",
      FF_TR_EN("Decide begin-while-repeat loop\n"
               "If *flag* is nonzero, execution continues after the **while**.\n"
               "If *flag* is zero, the loop is exited and execution resumed\n"
               "after the **repeat** that marks the end of the loop.\n"
               "\n"
               "A loop may have more than one **while**; each one after the first\n"
               "exits to the matching **then** after the **repeat**:\n"
               "\n"
               "    begin ... while ... while ... repeat ... then"),
      FF_TR_RU("Условие цикла begin-while-repeat\n"
               "Если *flag* не равен нулю, выполнение продолжается после **while**.\n"
               "Если *flag* равен нулю, цикл завершается, и выполнение продолжается\n"
               "после **repeat**, отмечающего конец цикла.\n"
               "\n"
               "В цикле может быть несколько **while**; каждое следующее после первого\n"
               "выходит к парному **then** после **repeat**:\n"
               "\n"
               "    begin ... while ... while ... repeat ... then")),
    _FF_WI("repeat", FF_OP_REPEAT, "( -- )",
      FF_TR_EN("Close begin-while-repeat loop\n"
               "Another iteration of the current **begin-while-repeat**\n"
               "loop having been completed, execution continues\n"
               "after the matching **begin**."),
      FF_TR_RU("Закрыть цикл begin-while-repeat\n"
               "Очередная итерация цикла **begin-while-repeat** завершена,\n"
               "и выполнение продолжается после парного **begin**.")),
    _FF_WI("do", FF_OP_DO, "( limit n -- )",
      FF_TR_EN("Definite loop\n"
               "Executes the loop from the following word to the matching **loop**\n"
               "or **+loop** until n increments past the boundary between *limit-1*\n"
               "and *limit*. Note that the loop is always executed at least once\n"
               "(see **?do** for an alternative to this)."),
      FF_TR_RU("Цикл со счётчиком\n"
               "Выполняет цикл от следующего слова до парного **loop** или **+loop**,\n"
               "пока n не перейдёт границу между *limit-1* и *limit*. Цикл всегда\n"
               "выполняется хотя бы один раз (альтернатива — **?do**).")),
    _FF_WI("?do", FF_OP_QDO, "( limit n -- )",
      FF_TR_EN("Conditional loop\n"
               "If *n* equals *limit*, skip immediately to the matching **loop**\n"
               "or **+loop**. Otherwise, enter the loop, which is thenceforth\n"
               "treated as a normal **do** loop."),
      FF_TR_RU("Условный цикл\n"
               "Если *n* равно *limit*, сразу переходит к парному **loop**\n"
               "или **+loop**. Иначе входит в цикл, который дальше работает\n"
               "как обычный цикл **do**.")),
    _FF_WI("loop", FF_OP_LOOP, "( -- )",
      FF_TR_EN("Increment loop index\n"
               "Adds one to the index of the active loop. If the limit\n"
               "is reached, the loop is exited. Otherwise, another iteration is begun."),
      FF_TR_RU("Увеличить индекс цикла\n"
               "Прибавляет единицу к индексу активного цикла. Если предел\n"
               "достигнут, цикл завершается. Иначе начинается следующая итерация.")),
    _FF_WI("+loop", FF_OP_PLOOP, "( n -- )",
      FF_TR_EN("Add to loop index\n"
               "Adds *n* to the index of the active loop. If the limit is reached,\n"
               "the loop is exited. Otherwise, another iteration is begun."),
      FF_TR_RU("Прибавить к индексу цикла\n"
               "Прибавляет *n* к индексу активного цикла. Если предел достигнут,\n"
               "цикл завершается. Иначе начинается следующая итерация.")),
    _FF_W("(xdo)", FF_OP_XDO, "( limit n -- )",
      FF_TR_EN("Execute loop\n"
               "At runtime, enters a loop that will step until *n* increments\n"
               "and becomes equal to *limit*."),
      FF_TR_RU("Выполнить цикл\n"
               "Во время выполнения входит в цикл, который повторяется,\n"
               "пока *n*, увеличиваясь, не станет равным *limit*.")),
    _FF_W("(x?do)", FF_OP_XQDO, "( limit n -- )",
      FF_TR_EN("Execute conditional loop\n"
               "At runtime, tests if *n* equals *limit*. If so, skips until the\n"
               "matching **loop** or **+loop**. Otherwise, enters the loop."),
      FF_TR_RU("Выполнить условный цикл\n"
               "Во время выполнения проверяет, равно ли *n* значению *limit*. Если да,\n"
               "переходит к парному **loop** или **+loop**. Иначе входит в цикл.")),
    _FF_W("(xloop)", FF_OP_XLOOP, "( -- )",
      FF_TR_EN("Increment loop index\n"
               "At runtime, adds one to the index of the active loop and exits\n"
               "if equal to the limit. Otherwise returns to the matching **do**\n"
               "or **?do**."),
      FF_TR_RU("Увеличить индекс цикла\n"
               "Во время выполнения прибавляет единицу к индексу активного цикла\n"
               "и завершает цикл, если индекс достиг предела. Иначе возвращается\n"
               "к парному **do** или **?do**.")),
    _FF_W("(+xloop)", FF_OP_PXLOOP, "( incr -- )",
      FF_TR_EN("Add to loop index\n"
               "At runtime, increments the loop index by the top of stack.\n"
               "If the loop is not done, begins the next iteration."),
      FF_TR_RU("Прибавить к индексу цикла\n"
               "Во время выполнения увеличивает индекс цикла на значение с вершины стека.\n"
               "Если цикл не закончен, начинает следующую итерацию.")),
    _FF_W("i", FF_OP_LOOP_I, "( -- n )",
      FF_TR_EN("Inner loop index\n"
               "The index of the innermost **do-loop** is placed on the stack."),
      FF_TR_RU("Индекс внутреннего цикла\n"
               "Кладёт на стек индекс самого внутреннего цикла **do-loop**.")),
    _FF_W("j", FF_OP_LOOP_J, "( -- n )",
      FF_TR_EN("Outer loop index\n"
               "The loop index of the next to innermost **do-loop** is placed on the stack."),
      FF_TR_RU("Индекс внешнего цикла\n"
               "Кладёт на стек индекс цикла **do-loop**, объемлющего самый внутренний.")),
    _FF_W("leave", FF_OP_LEAVE, "( -- )",
      FF_TR_EN("Exit do-loop\n"
               "The innermost **do-loop** is immediately exited. Execution resumes\n"
               "after the **loop** statement marking the end of the loop. A **{**\n"
               "scope opened inside the loop is closed first, as **}** would close\n"
               "it. Only valid inside a **do-loop** of the same definition."),
      FF_TR_RU("Выйти из do-loop\n"
               "Немедленно завершает самый внутренний цикл **do-loop**. Выполнение\n"
               "продолжается после **loop**, отмечающего конец цикла. Область **{**,\n"
               "открытая внутри цикла, сначала закрывается, как её закрыло бы **}**.\n"
               "Допустимо только внутри **do-loop** того же определения.")),
    _FF_W("quit", FF_OP_QUIT, "( -- )",
      FF_TR_EN("Quit execution\n"
               "The return stack is cleared and control is returned to the interpreter,\n"
               "discarding the rest of the input. The stack is not disturbed.\n"
               "No **catch** stops it."),
      FF_TR_RU("Прекратить выполнение\n"
               "Стек возвратов очищается, и управление возвращается интерпретатору;\n"
               "остаток ввода отбрасывается. Стек данных не меняется.\n"
               "Никакой **catch** его не останавливает.")),
    _FF_W("abort", FF_OP_ABORT, "( -- )",
      FF_TR_EN("Abort\n"
               "Performs `-1 throw`. If nothing catches it, the stacks are cleared\n"
               "and control returns to the interpreter, as with **quit**."),
      FF_TR_RU("Прервать\n"
               "Выполняет `-1 throw`. Если его никто не перехватит, стеки очищаются,\n"
               "и управление возвращается интерпретатору, как при **quit**.")),
    _FF_WI("abort\"", FF_OP_ABORTQ, "s ( -- )",
      FF_TR_EN("Abort with message\n"
               "Performs `-2 throw` with the string literal *s* that follows in line\n"
               "as the error message. If nothing catches it, it aborts like **abort**.\n"
               "In a definition the throw happens when the definition runs; at the\n"
               "prompt it happens at once."),
      FF_TR_RU("Прервать с сообщением\n"
               "Выполняет `-2 throw`, сообщение об ошибке — строковый литерал *s*,\n"
               "записанный сразу за словом. Если его никто не перехватит, прерывает\n"
               "выполнение, как **abort**. В определении исключение возникает, когда\n"
               "определение выполняется; в командной строке — сразу.")),
    _FF_W("throw", FF_OP_THROW, "( n -- )",
      FF_TR_EN("Raise exception\n"
               "If *n* is zero, **throw** is a no-op. Otherwise execution stops and\n"
               "unwinds to the most recent **catch**, which restores the stacks it\n"
               "saved and pushes *n*. If nothing catches it, the evaluation ends\n"
               "with an error.\n"
               "\n"
               "Errors are exceptions too, with the standard codes: -4 for stack\n"
               "underflow, -10 for division by zero, -13 for an undefined word, and\n"
               "so on."),
      FF_TR_RU("Выбросить исключение\n"
               "Если *n* равно нулю, **throw** ничего не делает. Иначе выполнение\n"
               "останавливается и раскручивается до последнего **catch**, который\n"
               "восстанавливает сохранённые стеки и кладёт *n*. Если исключение никто\n"
               "не перехватит, вычисление завершается ошибкой.\n"
               "\n"
               "Ошибки — тоже исключения, со стандартными кодами: -4 — исчерпание\n"
               "стека, -10 — деление на ноль, -13 — неопределённое слово и так далее.")),
    _FF_W("catch", FF_OP_CATCH, "( xt -- 0 | n )",
      FF_TR_EN("Catch exception\n"
               "Executes *xt*. If it returns normally, **catch** pushes 0.\n"
               "If an exception is raised during execution — a **throw**, an error,\n"
               "**abort** — the stacks are restored to the snapshot taken at this\n"
               "**catch** call and its code *n* is left on the data stack instead\n"
               "(-4 for stack underflow, -1 for **abort**, and so on).\n"
               "\n"
               "**quit** and the host's watchdog abort pass through every **catch**."),
      FF_TR_RU("Перехватить исключение\n"
               "Выполняет *xt*. Если оно завершается нормально, **catch** кладёт 0.\n"
               "Если во время выполнения возникает исключение — **throw**, ошибка,\n"
               "**abort**, — стеки восстанавливаются к снимку, сделанному при этом\n"
               "вызове **catch**, а на стек данных вместо этого кладётся его код *n*\n"
               "(-4 — исчерпание стека, -1 — **abort** и так далее).\n"
               "\n"
               "**quit** и прерывание по сторожевому таймеру хоста проходят\n"
               "через любой **catch**.")),
    FF_WEND
};

