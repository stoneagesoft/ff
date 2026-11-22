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
    _FF_W("evaluate", FF_OP_EVALUATE, "( s -- stat )",
      FF_TR_EN("Evaluate string\n"
               "Evaluates the source text *s* and leaves its status: zero if it ran\n"
               "to the end, otherwise the code of the exception that stopped it, as\n"
               "**catch** would leave (-13 for an undefined word, -10 for division\n"
               "by zero, the value given to **throw**, ...). The exception goes no\n"
               "further. **quit** and the host's watchdog abort do."),
      FF_TR_RU("Вычислить строку\n"
               "Вычисляет исходный текст *s* и оставляет его статус: ноль, если текст\n"
               "выполнился до конца, иначе код остановившего его исключения — такой же,\n"
               "какой оставил бы **catch** (-13 — неопределённое слово, -10 — деление\n"
               "на ноль, значение, переданное **throw**, ...). Дальше исключение\n"
               "не идёт. **quit** и прерывание по сторожевому таймеру хоста — идут.")),
#if FF_WITH_FILES
    _FF_W("load", FF_OP_LOAD, "( path -- stat )",
      FF_TR_EN("Load file\n"
               "The source program is loaded from the file as if its text\n"
               "appeared at the current character position in the input stream.\n"
               "The status resulting from the evaluation is left on the stack,\n"
               "zero if normal, negative in case of error (-37 if the file can't\n"
               "be opened), as for **evaluate**. The host may deny it.\n"
               "\n"
               "See also: **evaluate**"),
      FF_TR_RU("Загрузить файл\n"
               "Исходный текст программы загружается из файла так, как если бы он\n"
               "стоял на текущей позиции входного потока. Статус вычисления остаётся\n"
               "на стеке: ноль, если всё прошло нормально, отрицательный при ошибке\n"
               "(-37, если файл не открывается), как у **evaluate**. Хост может это\n"
               "запретить.\n"
               "\n"
               "См. также: **evaluate**")),
#endif
    _FF_W("parse-word", FF_OP_PARSE_WORD, "( -- s )",
      FF_TR_EN("Parse next token\n"
               "Parses the next whitespace-delimited token from the input stream\n"
               "and leaves it as a NUL-terminated string. At end of input the\n"
               "string is empty (`strlen` 0). Unlike `'`, the token is returned as\n"
               "text and is not looked up in the dictionary — this is the primitive\n"
               "for writing parsing words in Forth. Combine with **evaluate** to\n"
               "define new notation without extending the engine in C.\n"
               "\n"
               "See also: **parse**, **evaluate**"),
      FF_TR_RU("Прочитать следующую лексему\n"
               "Читает из входного потока следующую лексему, ограниченную пробелами,\n"
               "и оставляет её строкой с завершающим NUL. В конце ввода строка пустая\n"
               "(`strlen` 0). В отличие от `'`, лексема возвращается текстом и не ищется\n"
               "в словаре — это примитив для написания на Форте слов, разбирающих ввод.\n"
               "Вместе с **evaluate** позволяет вводить новую нотацию, не расширяя\n"
               "движок на C.\n"
               "\n"
               "См. также: **parse**, **evaluate**")),
    _FF_W("parse", FF_OP_PARSE, "( char -- s )",
      FF_TR_EN("Parse to delimiter\n"
               "Parses text from the current input position up to and including the\n"
               "next occurrence of *char* (given as its character code), and leaves\n"
               "the text before it as a NUL-terminated string. Leading delimiters\n"
               "are not skipped, so successive calls can return empty strings.\n"
               "Without the delimiter, it takes the rest of the input: `0 parse`\n"
               "always does.\n"
               "\n"
               "    41 parse group) type   \\ 41 is ')' — prints 'group'\n"
               "\n"
               "See also: **parse-word**"),
      FF_TR_RU("Прочитать до разделителя\n"
               "Читает текст от текущей позиции ввода до следующего вхождения *char*\n"
               "(заданного кодом символа) включительно и оставляет текст перед ним\n"
               "строкой с завершающим NUL. Начальные разделители не пропускаются,\n"
               "поэтому последовательные вызовы могут возвращать пустые строки.\n"
               "Без разделителя забирает остаток ввода: `0 parse` делает так всегда.\n"
               "\n"
               "    41 parse group) type   \\ 41 — это ')', печатает 'group'\n"
               "\n"
               "См. также: **parse-word**")),
    FF_WEND
};

