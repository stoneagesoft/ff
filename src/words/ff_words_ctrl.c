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
    _FF_W("exit", FF_OP_EXIT,
      "( -- )  Exit definition\n"
      "Exit from the current definition immediately. Note that\n"
      "**exit** cannot be used within a **do-loop**; use **leave** instead."),
    _FF_W("branch", FF_OP_BRANCH,
      "( -- )  Branch\n"
      "Jump to the address that follows in line."),
    _FF_W("?branch", FF_OP_QBRANCH,
      "( flag -- )  Conditional branch\n"
      "If the top of stack is zero, jump to the address which\n"
      "follows in line. Otherwise skip the address and continue execution."),
    _FF_W("?dup", FF_OP_QDUP,
      "( n -- 0 / n n )  Conditional duplicate\n"
      "If top of stack is nonzero, duplicate it.\n"
      "Otherwise leave zero on top of stack."),
    _FF_WI("if", FF_OP_IF,
      "( flag -- )  Conditional statement\n"
      "If *flag* is nonzero, the following statements are executed.\n"
      "Otherwise, execution resumes after the matching *else* clause,\n"
      "if any, or after the matching **then**."),
    _FF_WI("else", FF_OP_ELSE,
      "( -- )  Else\n"
      "Used in an **if-else-then** sequence, delimits the code\n"
      "to be executed if the if-condition was false."),
    _FF_WI("then", FF_OP_THEN,
      "( -- )  End if\n"
      "Used in an **if-else-then** sequence, marks the end of\n"
      "the conditional statement."),
    _FF_WI("begin", FF_OP_BEGIN,
      "( -- )  Begin loop\n"
      "Begins an indefinite loop. The end of the loop\n"
      "is marked by the matching **again**, **repeat**, or **until**."),
    _FF_WI("until", FF_OP_UNTIL,
      "( flag -- )  End begin-until loop\n"
      "If *flag* is zero, the loop continues execution at\n"
      "the word following the matching **begin**. If *flag* is nonzero,\n"
      "the loop is exited and the word following the **until** is executed."),
    _FF_WI("again", FF_OP_AGAIN,
      "( -- )  Indefinite loop\n"
      "Marks the end of an indefinite loop opened by the matching **begin**."),
    _FF_WI("while", FF_OP_WHILE,
      "( flag -- )  Decide begin-while-repeat loop\n"
      "If *flag* is nonzero, execution continues after the **while**.\n"
      "If *flag* is zero, the loop is exited and execution resumed\n"
      "after the **repeat** that marks the end of the loop."),
    _FF_WI("repeat", FF_OP_REPEAT,
      "( -- )  Close begin-while-repeat loop\n"
      "Another iteration of the current **begin-while-repeat**\n"
      "loop having been completed, execution continues\n"
      "after the matching **begin**."),
    _FF_WI("do", FF_OP_DO,
      "( limit n -- )  Definite loop\n"
      "Executes the loop from the following word to the matching **loop**\n"
      "or **+loop** until n increments past the boundary between *limit-1*\n"
      "and *limit*. Note that the loop is always executed at least once\n"
      "(see **?do** for an alternative to this)."),
    _FF_WI("?do", FF_OP_QDO,
      "( limit n -- )  Conditional loop\n"
      "If *n* equals *limit*, skip immediately to the matching **loop**\n"
      "or **+loop**. Otherwise, enter the loop, which is thenceforth\n"
      "treated as a normal **do** loop."),
    _FF_WI("loop", FF_OP_LOOP,
      "( -- )  Increment loop index\n"
      "Adds one to the index of the active loop. If the limit\n"
      "is reached, the loop is exited. Otherwise, another iteration is begun."),
    _FF_WI("+loop", FF_OP_PLOOP,
      "( n -- )  Add to loop index\n"
      "Adds *n* to the index of the active loop. If the limit is reached,\n"
      "the loop is exited. Otherwise, another iteration is begun."),
    _FF_W("(xdo)", FF_OP_XDO,
      "( limit n -- )  Execute loop\n"
      "At runtime, enters a loop that will step until *n* increments\n"
      "and becomes equal to *limit*."),
    _FF_W("(x?do)", FF_OP_XQDO,
      "( limit n -- )  Execute conditional loop\n"
      "At runtime, tests if *n* equals *limit*. If so, skips until the\n"
      "matching **loop** or **+loop**. Otherwise, enters the loop."),
    _FF_W("(xloop)", FF_OP_XLOOP,
      "( -- )  Increment loop index\n"
      "At runtime, adds one to the index of the active loop and exits\n"
      "if equal to the limit. Otherwise returns to the matching **do**\n"
      "or **?do**."),
    _FF_W("(+xloop)", FF_OP_PXLOOP,
      "( incr -- )  Add to loop index\n"
      "At runtime, increments the loop index by the top of stack.\n"
      "If the loop is not done, begins the next iteration."),
    _FF_W("i", FF_OP_LOOP_I,
      "( -- n )  Inner loop index\n"
      "The index of the innermost **do-loop** is placed on the stack."),
    _FF_W("j", FF_OP_LOOP_J,
      "( -- n )  Outer loop index\n"
      "The loop index of the next to innermost **do-loop** is placed on the stack."),
    _FF_W("leave", FF_OP_LEAVE,
      "( -- )  Exit do-loop\n"
      "The innermost **do-loop** is immediately exited. Execution resumes\n"
      "after the **loop** statement marking the end of the loop."),
    _FF_W("quit", FF_OP_QUIT,
      "( -- )  Quit execution\n"
      "The return stack is cleared and control is returned to the interpreter,\n"
      "discarding the rest of the input. The stack is not disturbed.\n"
      "No **catch** stops it."),
    _FF_W("abort", FF_OP_ABORT,
      "( -- )  Abort\n"
      "Performs `-1 throw`. If nothing catches it, the stacks are cleared\n"
      "and control returns to the interpreter, as with **quit**."),
    _FF_WI("abort\"", FF_OP_ABORTQ,
      "s ( -- )  Abort with message\n"
      "Performs `-2 throw` with the string literal *s* that follows in line\n"
      "as the error message. If nothing catches it, it aborts like **abort**."),
    _FF_W("throw", FF_OP_THROW,
      "( n -- )  Raise exception\n"
      "If *n* is zero, **throw** is a no-op. Otherwise execution stops and\n"
      "unwinds to the most recent **catch**, which restores the stacks it\n"
      "saved and pushes *n*. If nothing catches it, the evaluation ends\n"
      "with an error.\n"
      "\n"
      "Errors are exceptions too, with the standard codes: -4 for stack\n"
      "underflow, -10 for division by zero, -13 for an undefined word, and\n"
      "so on."),
    _FF_W("catch", FF_OP_CATCH,
      "( xt -- 0 | n )  Catch exception\n"
      "Executes *xt*. If it returns normally, **catch** pushes 0.\n"
      "If an exception is raised during execution — a **throw**, an error,\n"
      "**abort** — the stacks are restored to the snapshot taken at this\n"
      "**catch** call and its code *n* is left on the data stack instead\n"
      "(-4 for stack underflow, -1 for **abort**, and so on).\n"
      "\n"
      "**quit** and the host's watchdog abort pass through every **catch**."),
    FF_WEND
};

