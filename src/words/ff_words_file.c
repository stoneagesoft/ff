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
    _FF_W("system", FF_OP_SYSTEM,
      "( s -- n )  Execute system command\n"
      "Calls the host environment's command processor\n"
      "(*/bin/sh*, *cmd.exe*, *command.com*) with the parameter command.\n"
      "Returns an implementation-defined value (usually the value that\n"
      "the invoked program returns)."),
    _FF_W("stdin", FF_OP_STDIN,
      "( -- stream )  stdin file stream\n"
      "*stdin* file stream is left on the top of the stack.\n"
      "\n"
      "See also: **stdout**, **stderr**"),
    _FF_W("stdout", FF_OP_STDOUT,
      "( -- fd )  stdout file stream\n"
      "*stdout* file stream is left on the top of the stack.\n"
      "\n"
      "See also: **stdin**, **stderr**"),
    _FF_W("stderr", FF_OP_STDERR,
      "( -- fd )  stderr file stream\n"
      "*stderr* file stream is left on the top of the stack.\n"
      "\n"
      "See also: **stdin**, **stdout**"),
    _FF_W("fopen", FF_OP_FOPEN,
      "( mode path -- stream )  Open file\n"
      "Opens a file indicated by *path* and returns a file\n"
      "stream associated with that file. *mode* is used to\n"
      "determine the file access mode."),
    _FF_W("fclose", FF_OP_FCLOSE,
      "( stream -- n )  Close file\n"
      "Closes the given file stream *stream*. Any unwritten buffered\n"
      "data are flushed to the OS. Any unread buffered data are discarded.\n"
      "\n"
      "Returns *0* on success, *EOF* otherwise."),
    _FF_W("fgets", FF_OP_FGETS,
      "( stream count string -- string )  Read string\n"
      "Read at most *count - 1* characters from the given\n"
      "file stream and stores them in the character array\n"
      "pointed to by *string*."),
    _FF_W("fputs", FF_OP_FPUTS,
      "( stream string -- n )  Write string\n"
      "Writes every character from the null-terminated *string*\n"
      "to the output stream *stream*, as if by repeatedly executing **fputc**."),
    _FF_W("fgetc", FF_OP_FGETC,
      "( stream -- char )  Read next character\n"
      "Read the next character from the given input stream *stream*."),
    _FF_W("fputc", FF_OP_FPUTC,
      "( stream char -- char )  Write character\n"
      "Write a character *char* to the given output stream *stream*."),
    _FF_W("ftell", FF_OP_FTELL,
      "( stream -- pos )  File position\n"
      "Returns the current value of the file position indicator for\n"
      "the file stream *stream*."),
    _FF_W("fseek", FF_OP_FSEEK,
      "( stream origin offset -- )  Set file position\n"
      "Sets the file position indicator for the file stream *stream*.\n"
      "\n"
      "Returns *0* upon success, nonzero value otherwise."),
    _FF_W("seek_set", FF_OP_SEEK_SET,
      "( -- SEEK_SET )  SEEK_SET constant\n"
      "Leaves *SEEK_SET* constant on stack."),
    _FF_W("seek_cur", FF_OP_SEEK_CUR,
      "( -- SEEK_CUR )  SEEK_CUR constant\n"
      "Leaves *SEEK_CUR* constant on stack."),
    _FF_W("seek_end", FF_OP_SEEK_END,
      "( -- SEEK_END )  SEEK_END constant\n"
      "Leaves *SEEK_END* constant on stack."),
    _FF_W("ERRNO", FF_OP_ERRNO,
      "( -- errno )  C standard library error\n"
      "Several C standard library functions indicate errors by writing positive\n"
      "integers to *errno* C variable. This constant gives a read-only access to\n"
      "that variable.\n"
      "\n"
      "See also: **strerror**"),
    _FF_W("strerror", FF_OP_STRERROR,
      "( errnum -- s )  Textual error description\n"
      "Returns a pointer to the textual description of the system error code *errnum*.\n"
      "\n"
      "See also: **errno**"),
    FF_WEND
};

