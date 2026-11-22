/*
 * ff --- file word definitions.
 */

#include <ff_p.h>
#include <ff_word_def_p.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ===================================================================
 * File I/O words
 * =================================================================== */

const ff_word_def_t FF_FILE_WORDS[] =
{
#if FF_WITH_SYSTEM
    _FF_W("system", FF_OP_SYSTEM, "( s -- n )",
      FF_TR_EN("Execute system command\n"
               "Calls the host environment's command processor\n"
               "(*/bin/sh*, *cmd.exe*, *command.com*) with the parameter command.\n"
               "Returns an implementation-defined value (usually the value that\n"
               "the invoked program returns).\n"
               "\n"
               "The host may run commands its own way, or not at all."),
      FF_TR_RU("Выполнить команду системы\n"
               "Вызывает командный процессор среды хоста\n"
               "(*/bin/sh*, *cmd.exe*, *command.com*) с командой-параметром.\n"
               "Возвращает значение, определяемое реализацией (обычно значение,\n"
               "которое вернула запущенная программа).\n"
               "\n"
               "Хост может выполнять команды по-своему или не выполнять вовсе.")),
#endif
#if FF_WITH_FILES
    _FF_W("stdin", FF_OP_STDIN, "( -- stream )",
      FF_TR_EN("stdin file stream\n"
               "*stdin* file stream is left on the top of the stack.\n"
               "\n"
               "See also: **stdout**, **stderr**"),
      FF_TR_RU("Файловый поток stdin\n"
               "Оставляет на вершине стека файловый поток *stdin*.\n"
               "\n"
               "См. также: **stdout**, **stderr**")),
    _FF_W("stdout", FF_OP_STDOUT, "( -- fd )",
      FF_TR_EN("stdout file stream\n"
               "*stdout* file stream is left on the top of the stack.\n"
               "\n"
               "See also: **stdin**, **stderr**"),
      FF_TR_RU("Файловый поток stdout\n"
               "Оставляет на вершине стека файловый поток *stdout*.\n"
               "\n"
               "См. также: **stdin**, **stderr**")),
    _FF_W("stderr", FF_OP_STDERR, "( -- fd )",
      FF_TR_EN("stderr file stream\n"
               "*stderr* file stream is left on the top of the stack.\n"
               "\n"
               "See also: **stdin**, **stdout**"),
      FF_TR_RU("Файловый поток stderr\n"
               "Оставляет на вершине стека файловый поток *stderr*.\n"
               "\n"
               "См. также: **stdin**, **stdout**")),
    _FF_W("fopen", FF_OP_FOPEN, "( mode path -- stream )",
      FF_TR_EN("Open file\n"
               "Opens a file indicated by *path* and returns a file\n"
               "stream associated with that file. *mode* is used to\n"
               "determine the file access mode.\n"
               "\n"
               "The host may confine which files can be opened, or deny file\n"
               "access altogether. Streams left open are closed when the engine\n"
               "is freed."),
      FF_TR_RU("Открыть файл\n"
               "Открывает файл, указанный *path*, и возвращает связанный с ним\n"
               "файловый поток. *mode* задаёт режим доступа к файлу.\n"
               "\n"
               "Хост может ограничить, какие файлы можно открывать, или вовсе\n"
               "запретить доступ к файлам. Оставшиеся открытыми потоки закрываются,\n"
               "когда движок освобождается.")),
    _FF_W("fclose", FF_OP_FCLOSE, "( stream -- n )",
      FF_TR_EN("Close file\n"
               "Closes the given file stream *stream*. Any unwritten buffered\n"
               "data are flushed to the OS. Any unread buffered data are discarded.\n"
               "\n"
               "Returns *0* on success, *EOF* otherwise."),
      FF_TR_RU("Закрыть файл\n"
               "Закрывает файловый поток *stream*. Незаписанные данные из буфера\n"
               "передаются ОС. Непрочитанные данные из буфера отбрасываются.\n"
               "\n"
               "Возвращает *0* при успехе, иначе *EOF*.")),
    _FF_W("fgets", FF_OP_FGETS, "( stream count string -- string )",
      FF_TR_EN("Read string\n"
               "Read at most *count - 1* characters from the given\n"
               "file stream and stores them in the character array\n"
               "pointed to by *string*."),
      FF_TR_RU("Прочитать строку\n"
               "Читает из файлового потока не больше *count - 1* символов\n"
               "и сохраняет их в массиве символов, на который указывает *string*.")),
    _FF_W("fputs", FF_OP_FPUTS, "( stream string -- n )",
      FF_TR_EN("Write string\n"
               "Writes every character from the null-terminated *string*\n"
               "to the output stream *stream*, as if by repeatedly executing **fputc**."),
      FF_TR_RU("Записать строку\n"
               "Записывает все символы строки *string*, завершённой нулём, в выходной\n"
               "поток *stream*, как если бы для каждого выполнялся **fputc**.")),
    _FF_W("fgetc", FF_OP_FGETC, "( stream -- char )",
      FF_TR_EN("Read next character\n"
               "Read the next character from the given input stream *stream*."),
      FF_TR_RU("Прочитать символ\n"
               "Читает следующий символ из входного потока *stream*.")),
    _FF_W("fputc", FF_OP_FPUTC, "( stream char -- char )",
      FF_TR_EN("Write character\n"
               "Write a character *char* to the given output stream *stream*."),
      FF_TR_RU("Записать символ\n"
               "Записывает символ *char* в выходной поток *stream*.")),
    _FF_W("ftell", FF_OP_FTELL, "( stream -- pos )",
      FF_TR_EN("File position\n"
               "Returns the current value of the file position indicator for\n"
               "the file stream *stream*."),
      FF_TR_RU("Позиция в файле\n"
               "Возвращает текущее значение указателя позиции\n"
               "файлового потока *stream*.")),
    _FF_W("fseek", FF_OP_FSEEK, "( stream origin offset -- )",
      FF_TR_EN("Set file position\n"
               "Sets the file position indicator for the file stream *stream*.\n"
               "\n"
               "Returns *0* upon success, nonzero value otherwise."),
      FF_TR_RU("Установить позицию в файле\n"
               "Устанавливает указатель позиции файлового потока *stream*.\n"
               "\n"
               "Возвращает *0* при успехе, иначе ненулевое значение.")),
    _FF_W("seek_set", FF_OP_SEEK_SET, "( -- SEEK_SET )",
      FF_TR_EN("SEEK_SET constant\n"
               "Leaves *SEEK_SET* constant on stack."),
      FF_TR_RU("Константа SEEK_SET\n"
               "Оставляет на стеке константу *SEEK_SET*.")),
    _FF_W("seek_cur", FF_OP_SEEK_CUR, "( -- SEEK_CUR )",
      FF_TR_EN("SEEK_CUR constant\n"
               "Leaves *SEEK_CUR* constant on stack."),
      FF_TR_RU("Константа SEEK_CUR\n"
               "Оставляет на стеке константу *SEEK_CUR*.")),
    _FF_W("seek_end", FF_OP_SEEK_END, "( -- SEEK_END )",
      FF_TR_EN("SEEK_END constant\n"
               "Leaves *SEEK_END* constant on stack."),
      FF_TR_RU("Константа SEEK_END\n"
               "Оставляет на стеке константу *SEEK_END*.")),
#endif
    _FF_W("ERRNO", FF_OP_ERRNO, "( -- errno )",
      FF_TR_EN("C standard library error\n"
               "Several C standard library functions indicate errors by writing positive\n"
               "integers to *errno* C variable. This constant gives a read-only access to\n"
               "that variable.\n"
               "\n"
               "See also: **strerror**"),
      FF_TR_RU("Ошибка стандартной библиотеки C\n"
               "Ряд функций стандартной библиотеки C сообщает об ошибках, записывая\n"
               "положительные целые числа в переменную C *errno*. Эта константа даёт\n"
               "доступ к этой переменной только для чтения.\n"
               "\n"
               "См. также: **strerror**")),
    _FF_W("strerror", FF_OP_STRERROR, "( errnum -- s )",
      FF_TR_EN("Textual error description\n"
               "Returns a pointer to the textual description of the system error code *errnum*.\n"
               "\n"
               "See also: **errno**"),
      FF_TR_RU("Текстовое описание ошибки\n"
               "Возвращает указатель на текстовое описание системного кода ошибки *errnum*.\n"
               "\n"
               "См. также: **errno**")),
    FF_WEND
};

