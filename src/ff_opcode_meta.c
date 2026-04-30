/**
 * @file ff_opcode_meta.c
 * @brief Metadata table indexed by opcode.
 *
 * Adding a new opcode (especially a peephole) means: register the
 * enum value in ff_opcode_p.h, add the case body in the dispatch
 * include, and add a row here. Tooling that walks bytecode reads
 * from this single table.
 */

#include "ff_opcode_meta_p.h"

#include <stddef.h>


/* The table is indexed by opcode value. FF_OP_NONE is -1 (the
   sentinel) so we don't store metadata for it; lookups for it
   return a single safe placeholder.

   Maintain the row order matching the enum in ff_opcode_p.h so a
   missing entry is visually obvious in code review. */
static const ff_opcode_meta_t g_meta[FF_OP_COUNT] = {
    [FF_OP_CALL]              = { NULL,        FF_OP_LAYOUT_FN   },
    [FF_OP_NEST]              = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_TNEST]             = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_EXIT]              = { "exit",      FF_OP_LAYOUT_NONE },
    [FF_OP_LIT]               = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_LIT0]              = { NULL,        FF_OP_LAYOUT_NONE },
    [FF_OP_LIT1]              = { NULL,        FF_OP_LAYOUT_NONE },
    [FF_OP_LITM1]             = { NULL,        FF_OP_LAYOUT_NONE },
    [FF_OP_LITADD]            = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_LITSUB]            = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_FLIT]              = { NULL,        FF_OP_LAYOUT_REAL },
    [FF_OP_STRLIT]            = { NULL,        FF_OP_LAYOUT_STR  },
    [FF_OP_BRANCH]            = { "branch",    FF_OP_LAYOUT_INT  },
    [FF_OP_QBRANCH]           = { "?branch",   FF_OP_LAYOUT_INT  },

    [FF_OP_DOES_RUNTIME]      = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_CREATE_RUNTIME]    = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_CONSTANT_RUNTIME]  = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_ARRAY_RUNTIME]     = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_DEFER_RUNTIME]     = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_VAR_FETCH]         = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_VAR_STORE]         = { NULL,        FF_OP_LAYOUT_WORD },
    [FF_OP_VAR_PLUS_STORE]    = { NULL,        FF_OP_LAYOUT_WORD },

    [FF_OP_DUP]               = { "dup",       FF_OP_LAYOUT_NONE },
    [FF_OP_DROP]              = { "drop",      FF_OP_LAYOUT_NONE },
    [FF_OP_SWAP]              = { "swap",      FF_OP_LAYOUT_NONE },
    [FF_OP_OVER]              = { "over",      FF_OP_LAYOUT_NONE },
    [FF_OP_ROT]               = { "rot",       FF_OP_LAYOUT_NONE },
    [FF_OP_NROT]              = { "-rot",      FF_OP_LAYOUT_NONE },
    [FF_OP_PICK]              = { "pick",      FF_OP_LAYOUT_NONE },
    [FF_OP_ROLL]              = { "roll",      FF_OP_LAYOUT_NONE },
    [FF_OP_DEPTH]             = { "depth",     FF_OP_LAYOUT_NONE },
    [FF_OP_CLEAR]             = { "clear",     FF_OP_LAYOUT_NONE },
    [FF_OP_TO_R]              = { ">r",        FF_OP_LAYOUT_NONE },
    [FF_OP_FROM_R]            = { "r>",        FF_OP_LAYOUT_NONE },
    [FF_OP_FETCH_R]           = { "r@",        FF_OP_LAYOUT_NONE },

    [FF_OP_2DUP]              = { "2dup",      FF_OP_LAYOUT_NONE },
    [FF_OP_2DROP]             = { "2drop",     FF_OP_LAYOUT_NONE },
    [FF_OP_2SWAP]             = { "2swap",     FF_OP_LAYOUT_NONE },
    [FF_OP_2OVER]             = { "2over",     FF_OP_LAYOUT_NONE },

    [FF_OP_ADD]               = { "+",         FF_OP_LAYOUT_NONE },
    [FF_OP_SUB]               = { "-",         FF_OP_LAYOUT_NONE },
    [FF_OP_MUL]               = { "*",         FF_OP_LAYOUT_NONE },
    [FF_OP_DIV]               = { "/",         FF_OP_LAYOUT_NONE },
    [FF_OP_MOD]               = { "mod",       FF_OP_LAYOUT_NONE },
    [FF_OP_DIVMOD]            = { "/mod",      FF_OP_LAYOUT_NONE },
    [FF_OP_MIN]               = { "min",       FF_OP_LAYOUT_NONE },
    [FF_OP_MAX]               = { "max",       FF_OP_LAYOUT_NONE },
    [FF_OP_NEGATE]            = { "negate",    FF_OP_LAYOUT_NONE },
    [FF_OP_ABS]               = { "abs",       FF_OP_LAYOUT_NONE },
    [FF_OP_AND]               = { "and",       FF_OP_LAYOUT_NONE },
    [FF_OP_OR]                = { "or",        FF_OP_LAYOUT_NONE },
    [FF_OP_XOR]               = { "xor",       FF_OP_LAYOUT_NONE },
    [FF_OP_NOT]               = { "not",       FF_OP_LAYOUT_NONE },
    [FF_OP_SHIFT]             = { "shift",     FF_OP_LAYOUT_NONE },
    [FF_OP_EQ]                = { "=",         FF_OP_LAYOUT_NONE },
    [FF_OP_NEQ]               = { "<>",        FF_OP_LAYOUT_NONE },
    [FF_OP_LT]                = { "<",         FF_OP_LAYOUT_NONE },
    [FF_OP_GT]                = { ">",         FF_OP_LAYOUT_NONE },
    [FF_OP_LE]                = { "<=",        FF_OP_LAYOUT_NONE },
    [FF_OP_GE]                = { ">=",        FF_OP_LAYOUT_NONE },
    [FF_OP_ZERO_EQ]           = { "0=",        FF_OP_LAYOUT_NONE },
    [FF_OP_ZERO_NEQ]          = { "0<>",       FF_OP_LAYOUT_NONE },
    [FF_OP_ZERO_LT]           = { "0<",        FF_OP_LAYOUT_NONE },
    [FF_OP_ZERO_GT]           = { "0>",        FF_OP_LAYOUT_NONE },
    [FF_OP_INC]               = { "1+",        FF_OP_LAYOUT_NONE },
    [FF_OP_DEC]               = { "1-",        FF_OP_LAYOUT_NONE },
    [FF_OP_INC2]              = { "2+",        FF_OP_LAYOUT_NONE },
    [FF_OP_DEC2]              = { "2-",        FF_OP_LAYOUT_NONE },
    [FF_OP_MUL2]              = { "2*",        FF_OP_LAYOUT_NONE },
    [FF_OP_DIV2]              = { "2/",        FF_OP_LAYOUT_NONE },
    [FF_OP_SET_BASE]          = { "base",      FF_OP_LAYOUT_NONE },

    [FF_OP_FADD]              = { "f+",        FF_OP_LAYOUT_NONE },
    [FF_OP_FSUB]              = { "f-",        FF_OP_LAYOUT_NONE },
    [FF_OP_FMUL]              = { "f*",        FF_OP_LAYOUT_NONE },
    [FF_OP_FDIV]              = { "f/",        FF_OP_LAYOUT_NONE },
    [FF_OP_FNEGATE]           = { "fnegate",   FF_OP_LAYOUT_NONE },
    [FF_OP_FABS]              = { "fabs",      FF_OP_LAYOUT_NONE },
    [FF_OP_FSQRT]             = { "sqrt",      FF_OP_LAYOUT_NONE },
    [FF_OP_FSIN]              = { "sin",       FF_OP_LAYOUT_NONE },
    [FF_OP_FCOS]              = { "cos",       FF_OP_LAYOUT_NONE },
    [FF_OP_FTAN]              = { "tan",       FF_OP_LAYOUT_NONE },
    [FF_OP_FASIN]             = { "asin",      FF_OP_LAYOUT_NONE },
    [FF_OP_FACOS]             = { "acos",      FF_OP_LAYOUT_NONE },
    [FF_OP_FATAN]             = { "atan",      FF_OP_LAYOUT_NONE },
    [FF_OP_FATAN2]            = { "atan2",     FF_OP_LAYOUT_NONE },
    [FF_OP_FEXP]              = { "exp",       FF_OP_LAYOUT_NONE },
    [FF_OP_FLOG]              = { "log",       FF_OP_LAYOUT_NONE },
    [FF_OP_FPOW]              = { "pow",       FF_OP_LAYOUT_NONE },
    [FF_OP_F_DOT]             = { "f.",        FF_OP_LAYOUT_NONE },
    [FF_OP_FLOAT]             = { "float",     FF_OP_LAYOUT_NONE },
    [FF_OP_FIX]               = { "fix",       FF_OP_LAYOUT_NONE },
    [FF_OP_PI]                = { "pi",        FF_OP_LAYOUT_NONE },
    [FF_OP_E_CONST]           = { "e",         FF_OP_LAYOUT_NONE },
    [FF_OP_FEQ]               = { "f=",        FF_OP_LAYOUT_NONE },
    [FF_OP_FNEQ]              = { "f<>",       FF_OP_LAYOUT_NONE },
    [FF_OP_FLT]               = { "f<",        FF_OP_LAYOUT_NONE },
    [FF_OP_FGT]               = { "f>",        FF_OP_LAYOUT_NONE },
    [FF_OP_FLE]               = { "f<=",       FF_OP_LAYOUT_NONE },
    [FF_OP_FGE]               = { "f>=",       FF_OP_LAYOUT_NONE },

    [FF_OP_DOT]               = { ".",         FF_OP_LAYOUT_NONE },
    [FF_OP_QUESTION]          = { "?",         FF_OP_LAYOUT_NONE },
    [FF_OP_CR]                = { "cr",        FF_OP_LAYOUT_NONE },
    [FF_OP_EMIT]              = { "emit",      FF_OP_LAYOUT_NONE },
    [FF_OP_TYPE]              = { "type",      FF_OP_LAYOUT_NONE },
    [FF_OP_DOT_S]             = { ".s",        FF_OP_LAYOUT_NONE },
    [FF_OP_DOT_PAREN]         = { ".(",        FF_OP_LAYOUT_NONE },
    [FF_OP_DOTQUOTE]          = { ".\"",       FF_OP_LAYOUT_NONE },

    [FF_OP_XDO]               = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_XQDO]              = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_XLOOP]             = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_PXLOOP]            = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_LOOP_I]            = { "i",         FF_OP_LAYOUT_NONE },
    [FF_OP_LOOP_J]            = { "j",         FF_OP_LAYOUT_NONE },
    [FF_OP_LEAVE]             = { "leave",     FF_OP_LAYOUT_NONE },
    [FF_OP_I_ADD]             = { NULL,        FF_OP_LAYOUT_NONE },
    [FF_OP_I_ADD_LOOP]        = { NULL,        FF_OP_LAYOUT_INT  },
    [FF_OP_NIP]               = { "nip",       FF_OP_LAYOUT_NONE },
    [FF_OP_TUCK]              = { "tuck",      FF_OP_LAYOUT_NONE },
    [FF_OP_OVER_PLUS]         = { NULL,        FF_OP_LAYOUT_NONE },
    [FF_OP_R_PLUS]            = { NULL,        FF_OP_LAYOUT_NONE },

    [FF_OP_COLON]             = { ":",         FF_OP_LAYOUT_NONE },
    [FF_OP_SEMICOLON]         = { ";",         FF_OP_LAYOUT_NONE },
    [FF_OP_IMMEDIATE]         = { "immediate", FF_OP_LAYOUT_NONE },
    [FF_OP_LBRACKET]          = { "[",         FF_OP_LAYOUT_NONE },
    [FF_OP_RBRACKET]          = { "]",         FF_OP_LAYOUT_NONE },
    [FF_OP_TICK]              = { "'",         FF_OP_LAYOUT_NONE },
    [FF_OP_BRACKET_TICK]      = { "[']",       FF_OP_LAYOUT_NONE },
    [FF_OP_EXECUTE]           = { "execute",   FF_OP_LAYOUT_NONE },
    [FF_OP_STATE]             = { "state",     FF_OP_LAYOUT_NONE },
    [FF_OP_BRACKET_COMPILE]   = { "[compile]", FF_OP_LAYOUT_NONE },
    [FF_OP_LITERAL]           = { "literal",   FF_OP_LAYOUT_NONE },
    [FF_OP_COMPILE]           = { "compile",   FF_OP_LAYOUT_NONE },
    [FF_OP_DOES]              = { "does>",     FF_OP_LAYOUT_NONE },

    [FF_OP_QDUP]              = { "?dup",      FF_OP_LAYOUT_NONE },
    [FF_OP_IF]                = { "if",        FF_OP_LAYOUT_NONE },
    [FF_OP_ELSE]              = { "else",      FF_OP_LAYOUT_NONE },
    [FF_OP_THEN]              = { "then",      FF_OP_LAYOUT_NONE },
    [FF_OP_BEGIN]             = { "begin",     FF_OP_LAYOUT_NONE },
    [FF_OP_UNTIL]             = { "until",     FF_OP_LAYOUT_NONE },
    [FF_OP_AGAIN]             = { "again",     FF_OP_LAYOUT_NONE },
    [FF_OP_WHILE]             = { "while",     FF_OP_LAYOUT_NONE },
    [FF_OP_REPEAT]            = { "repeat",    FF_OP_LAYOUT_NONE },
    [FF_OP_DO]                = { "do",        FF_OP_LAYOUT_NONE },
    [FF_OP_QDO]               = { "?do",       FF_OP_LAYOUT_NONE },
    [FF_OP_LOOP]              = { "loop",      FF_OP_LAYOUT_NONE },
    [FF_OP_PLOOP]             = { "+loop",     FF_OP_LAYOUT_NONE },
    [FF_OP_QUIT]              = { "quit",      FF_OP_LAYOUT_NONE },
    [FF_OP_ABORT]             = { "abort",     FF_OP_LAYOUT_NONE },
    [FF_OP_THROW]             = { "throw",     FF_OP_LAYOUT_NONE },
    [FF_OP_CATCH]             = { "catch",     FF_OP_LAYOUT_NONE },
    [FF_OP_ABORTQ]            = { "abort\"",   FF_OP_LAYOUT_NONE },

    [FF_OP_CREATE]            = { "create",    FF_OP_LAYOUT_NONE },
    [FF_OP_FORGET]            = { "forget",    FF_OP_LAYOUT_NONE },
    [FF_OP_VARIABLE]          = { "variable",  FF_OP_LAYOUT_NONE },
    [FF_OP_CONSTANT]          = { "constant",  FF_OP_LAYOUT_NONE },
    [FF_OP_DEFER]             = { "defer",     FF_OP_LAYOUT_NONE },
    [FF_OP_IS]                = { "is",        FF_OP_LAYOUT_NONE },

    [FF_OP_HERE]              = { "here",      FF_OP_LAYOUT_NONE },
    [FF_OP_STORE]             = { "!",         FF_OP_LAYOUT_NONE },
    [FF_OP_FETCH]             = { "@",         FF_OP_LAYOUT_NONE },
    [FF_OP_PLUS_STORE]        = { "+!",        FF_OP_LAYOUT_NONE },
    [FF_OP_ALLOT]             = { "allot",     FF_OP_LAYOUT_NONE },
    [FF_OP_COMMA]             = { ",",         FF_OP_LAYOUT_NONE },
    [FF_OP_C_STORE]           = { "c!",        FF_OP_LAYOUT_NONE },
    [FF_OP_C_FETCH]           = { "c@",        FF_OP_LAYOUT_NONE },
    [FF_OP_C_COMMA]           = { "c,",        FF_OP_LAYOUT_NONE },
    [FF_OP_C_ALIGN]           = { "c=",        FF_OP_LAYOUT_NONE },

    [FF_OP_STRING]            = { "string",    FF_OP_LAYOUT_NONE },
    [FF_OP_S_STORE]           = { "s!",        FF_OP_LAYOUT_NONE },
    [FF_OP_S_CAT]             = { "s+",        FF_OP_LAYOUT_NONE },
    [FF_OP_STRLEN]            = { "strlen",    FF_OP_LAYOUT_NONE },
    [FF_OP_STRCMP]            = { "strcmp",    FF_OP_LAYOUT_NONE },

    [FF_OP_EVALUATE]          = { "evaluate",  FF_OP_LAYOUT_NONE },
    [FF_OP_LOAD]              = { "load",      FF_OP_LAYOUT_NONE },

    [FF_OP_FIND]              = { "find",      FF_OP_LAYOUT_NONE },
    [FF_OP_TO_NAME]           = { ">name",     FF_OP_LAYOUT_NONE },
    [FF_OP_TO_BODY]           = { ">body",     FF_OP_LAYOUT_NONE },

    [FF_OP_ARRAY]             = { "array",     FF_OP_LAYOUT_NONE },

    [FF_OP_SYSTEM]            = { "system",    FF_OP_LAYOUT_NONE },
    [FF_OP_STDIN]             = { "stdin",     FF_OP_LAYOUT_NONE },
    [FF_OP_STDOUT]            = { "stdout",    FF_OP_LAYOUT_NONE },
    [FF_OP_STDERR]            = { "stderr",    FF_OP_LAYOUT_NONE },
    [FF_OP_FOPEN]             = { "fopen",     FF_OP_LAYOUT_NONE },
    [FF_OP_FCLOSE]            = { "fclose",    FF_OP_LAYOUT_NONE },
    [FF_OP_FGETS]             = { "fgets",     FF_OP_LAYOUT_NONE },
    [FF_OP_FPUTS]             = { "fputs",     FF_OP_LAYOUT_NONE },
    [FF_OP_FGETC]             = { "fgetc",     FF_OP_LAYOUT_NONE },
    [FF_OP_FPUTC]             = { "fputc",     FF_OP_LAYOUT_NONE },
    [FF_OP_FTELL]             = { "ftell",     FF_OP_LAYOUT_NONE },
    [FF_OP_FSEEK]             = { "fseek",     FF_OP_LAYOUT_NONE },
    [FF_OP_SEEK_SET]          = { "seek_set",  FF_OP_LAYOUT_NONE },
    [FF_OP_SEEK_CUR]          = { "seek_cur",  FF_OP_LAYOUT_NONE },
    [FF_OP_SEEK_END]          = { "seek_end",  FF_OP_LAYOUT_NONE },
    [FF_OP_ERRNO]             = { "ERRNO",     FF_OP_LAYOUT_NONE },
    [FF_OP_STRERROR]          = { "strerror",  FF_OP_LAYOUT_NONE },

    [FF_OP_TRACE]             = { "trace",     FF_OP_LAYOUT_NONE },
    [FF_OP_BACKTRACE]         = { "backtrace", FF_OP_LAYOUT_NONE },
    [FF_OP_DUMP]              = { "dump",      FF_OP_LAYOUT_NONE },
    [FF_OP_MEMSTAT]           = { "memstat",   FF_OP_LAYOUT_NONE },

    [FF_OP_WORDS]             = { "words",     FF_OP_LAYOUT_NONE },
    [FF_OP_WORDSUSED]         = { "wordsused", FF_OP_LAYOUT_NONE },
    [FF_OP_WORDSUNUSED]       = { "wordsunused", FF_OP_LAYOUT_NONE },
    [FF_OP_MAN]               = { "man",       FF_OP_LAYOUT_NONE },
    [FF_OP_DUMP_WORD]         = { "dump-word", FF_OP_LAYOUT_NONE },
    [FF_OP_SEE]               = { "see",       FF_OP_LAYOUT_NONE },
};

static const ff_opcode_meta_t g_meta_unknown = { NULL, FF_OP_LAYOUT_NONE };


const ff_opcode_meta_t *ff_opcode_meta(ff_opcode_t op)
{
    if (op < 0 || op >= FF_OP_COUNT)
        return &g_meta_unknown;
    return &g_meta[op];
}

size_t ff_opcode_encoded_cells(ff_opcode_t op,
                               const ff_int_t *cells,
                               size_t pos, size_t size)
{
    const ff_opcode_meta_t *m = ff_opcode_meta(op);
    switch (m->layout)
    {
        case FF_OP_LAYOUT_NONE:
            return 1;
        case FF_OP_LAYOUT_INT:
        case FF_OP_LAYOUT_REAL:
        case FF_OP_LAYOUT_WORD:
        case FF_OP_LAYOUT_FN:
            return 2;
        case FF_OP_LAYOUT_STR:
            return 1 + (pos + 1 < size ? (size_t)cells[pos + 1] : 0);
    }
    return 1;
}
