/**
 * @file ff_opcode_p.h
 * @brief Opcodes for the central switch dispatch in the inner
 *        interpreter.
 *
 * Encoding in compiled bytecode:
 *
 * | Opcode                       | Cells              | Notes                                 |
 * |------------------------------|--------------------|---------------------------------------|
 * | @c FF_OP_NONE                | (none)             | Sentinel — never appears in bytecode. |
 * | @c FF_OP_CALL                | opcode + fn_ptr    | External native escape hatch.         |
 * | @c FF_OP_NEST / TNEST        | opcode + word_ptr  | Colon-def invocation / tail call.     |
 * | @c FF_OP_DOES_RUNTIME        | opcode + word_ptr  | DOES>-built word entry.               |
 * | @c FF_OP_CREATE_RUNTIME      | opcode + word_ptr  | CREATE-built word entry.              |
 * | @c FF_OP_CONSTANT_RUNTIME    | opcode + word_ptr  | CONSTANT-built word entry.            |
 * | @c FF_OP_ARRAY_RUNTIME       | opcode + word_ptr  | ARRAY-built word entry.               |
 * | @c FF_OP_LIT / FLIT / STRLIT | opcode + payload   | Inline literal data.                  |
 * | @c FF_OP_LITADD / LITSUB     | opcode + n         | Superinstruction (LIT + arith).       |
 * | @c FF_OP_BRANCH / QBRANCH    | opcode + offset    | Relative jump (negative = backward).  |
 * | @c FF_OP_XDO / XQDO / XLOOP  | opcode + offset    | Counted-loop scaffolding.             |
 * | every other opcode           | single cell        | No operand.                           |
 */

#pragma once

/**
 * @enum ff_opcode
 * @brief Identifier carried in each compiled cell.
 *
 * Numbered explicitly: @c FF_OP_NONE is -1 (sentinel) and the regular
 * opcodes start at 0 so they index a virtual jump table cleanly. The
 * trailing @c FF_OP_COUNT records the count for diagnostics.
 */
typedef enum ff_opcode
{
    FF_OP_NONE = -1,            /**< Sentinel: word has no opcode assigned (e.g. external native). */

    /* --- Structural / control flow --- */

    FF_OP_CALL = 0,             /**< + fn_ptr — external native word escape hatch. */
    FF_OP_NEST,                 /**< + word_ptr — colon-def invocation; pushes return frame. */
    FF_OP_TNEST,                /**< + word_ptr — tail-call NEST that replaces caller's frame. */
    FF_OP_EXIT,                 /**< Pop return stack and resume; falls out of ff_exec when sentinel-NULL is popped. */

    FF_OP_LIT,                  /**< + value — push an inline cell. */
    FF_OP_LIT0,                 /**< Push 0 — specialized for the most common literal. */
    FF_OP_LIT1,                 /**< Push 1. */
    FF_OP_LITM1,                /**< Push -1. */
    FF_OP_LITADD,               /**< + n — superinstruction: TOS += n. */
    FF_OP_LITSUB,               /**< + n — superinstruction: TOS -= n. */
    FF_OP_FLIT,                 /**< + value — push an inline real. */
    FF_OP_STRLIT,               /**< + skip count + packed bytes — push a pointer to the inline string. */

    FF_OP_BRANCH,               /**< + offset — unconditional jump. */
    FF_OP_QBRANCH,              /**< + offset — jump if TOS is zero (consumes TOS). */

    /* --- Runtimes for words made with create / does> / constant / array --- */

    FF_OP_DOES_RUNTIME,         /**< + word_ptr — DOES>-clause entry. */
    FF_OP_CREATE_RUNTIME,       /**< + word_ptr — push word's heap.data pointer. */
    FF_OP_CONSTANT_RUNTIME,     /**< + word_ptr — push word's heap.data[0]. */
    FF_OP_ARRAY_RUNTIME,        /**< + word_ptr — index into word's heap (TOS = base + idx). */
    FF_OP_DEFER_RUNTIME,        /**< + word_ptr — call through xt stored at heap.data[0] (ANS DEFER). */
    FF_OP_VAR_FETCH,            /**< + word_ptr — push word's heap.data[0] (peephole `v @`). */
    FF_OP_VAR_STORE,            /**< + word_ptr — pop, store at word's heap.data[0] (peephole `v !`). */
    FF_OP_VAR_PLUS_STORE,       /**< + word_ptr — pop, add to word's heap.data[0] (peephole `v +!`). */

    /* --- Stack manipulation --- */

    FF_OP_DUP,    /**< ( a -- a a ) */
    FF_OP_DROP,   /**< ( a -- ) */
    FF_OP_SWAP,   /**< ( a b -- b a ) */
    FF_OP_OVER,   /**< ( a b -- a b a ) */
    FF_OP_ROT,    /**< ( a b c -- b c a ) */
    FF_OP_NROT,   /**< ( a b c -- c a b ) */
    FF_OP_PICK,   /**< ( … n -- … item-at-depth-n ) */
    FF_OP_ROLL,   /**< Rotate item at depth n to TOS. */
    FF_OP_DEPTH,  /**< ( -- n ) push current data-stack depth. */
    FF_OP_CLEAR,  /**< Drop every data-stack item. */
    FF_OP_TO_R,   /**< ( a -- )  R: ( -- a ) — move TOS to return stack. */
    FF_OP_FROM_R, /**< Inverse of FF_OP_TO_R. */
    FF_OP_FETCH_R,/**< Copy R's TOS to data stack. */

    /* --- Double-cell stack ops --- */

    FF_OP_2DUP,   /**< ( a b -- a b a b ) */
    FF_OP_2DROP,  /**< ( a b -- ) */
    FF_OP_2SWAP,  /**< ( a b c d -- c d a b ) */
    FF_OP_2OVER,  /**< ( a b c d -- a b c d a b ) */

    /* --- Integer math and comparisons --- */

    FF_OP_ADD, FF_OP_SUB, FF_OP_MUL, FF_OP_DIV, FF_OP_MOD, FF_OP_DIVMOD,
    FF_OP_MIN, FF_OP_MAX, FF_OP_NEGATE, FF_OP_ABS,
    FF_OP_AND, FF_OP_OR, FF_OP_XOR, FF_OP_NOT, FF_OP_SHIFT,
    FF_OP_EQ, FF_OP_NEQ, FF_OP_LT, FF_OP_GT, FF_OP_LE, FF_OP_GE,
    FF_OP_ZERO_EQ, FF_OP_ZERO_NEQ, FF_OP_ZERO_LT, FF_OP_ZERO_GT,
    FF_OP_INC, FF_OP_DEC, FF_OP_INC2, FF_OP_DEC2, FF_OP_MUL2, FF_OP_DIV2,
    FF_OP_SET_BASE,             /**< Pop n, set the print/parse base to n (10 or 16). */

    /* --- Floating-point --- */

    FF_OP_FADD, FF_OP_FSUB, FF_OP_FMUL, FF_OP_FDIV,
    FF_OP_FNEGATE, FF_OP_FABS, FF_OP_FSQRT,
    FF_OP_FSIN, FF_OP_FCOS, FF_OP_FTAN,
    FF_OP_FASIN, FF_OP_FACOS, FF_OP_FATAN, FF_OP_FATAN2,
    FF_OP_FEXP, FF_OP_FLOG, FF_OP_FPOW,
    FF_OP_F_DOT,                /**< Print top-of-stack as a real (`f.`). */
    FF_OP_FLOAT,                /**< Convert TOS int → real bit-pattern. */
    FF_OP_FIX,                  /**< Convert TOS real → truncated int. */
    FF_OP_PI,                   /**< Push PI. */
    FF_OP_E_CONST,              /**< Push e. */
    FF_OP_FEQ, FF_OP_FNEQ, FF_OP_FLT, FF_OP_FGT, FF_OP_FLE, FF_OP_FGE,

    /* --- Console I/O --- */

    FF_OP_DOT,                  /**< Print TOS as integer in current base. */
    FF_OP_QUESTION,             /**< Print value at the address on TOS. */
    FF_OP_CR,                   /**< Print newline. */
    FF_OP_EMIT,                 /**< Print TOS as a single byte. */
    FF_OP_TYPE,                 /**< Print NUL-terminated string at TOS. */
    FF_OP_DOT_S,                /**< Print full data stack as a table. */
    FF_OP_DOT_PAREN,            /**< Compile/print inline `( … )` string. */
    FF_OP_DOTQUOTE,             /**< Compile a `."` literal-string-print. */

    /* --- Counted loops --- */

    FF_OP_XDO,                  /**< + offset — runtime DO entry. */
    FF_OP_XQDO,                 /**< + offset — runtime ?DO entry (skip body if start==limit). */
    FF_OP_XLOOP,                /**< + offset — runtime LOOP back-edge. */
    FF_OP_PXLOOP,               /**< + offset — runtime +LOOP back-edge. */
    FF_OP_LOOP_I,               /**< Push current loop index (`i`). */
    FF_OP_LOOP_J,               /**< Push outer loop index (`j`). */
    FF_OP_LEAVE,                /**< Exit innermost counted loop early. */
    FF_OP_I_ADD,                /**< Superinstruction: i + (add the loop index to TOS). */
    FF_OP_I_ADD_LOOP,           /**< Superinstruction: i + loop (fused index+ and loop back-edge). */
    FF_OP_NIP,                  /**< ( a b -- b ) — drop the second-from-top item. */
    FF_OP_TUCK,                 /**< ( a b -- b a b ) — copy TOS under NOS. */
    FF_OP_OVER_PLUS,            /**< Superinstruction: over + (TOS += NOS). */
    FF_OP_R_PLUS,               /**< Superinstruction: r@ + (add return-stack TOS to data TOS). */

    /* --- Compile-time / immediate --- */

    FF_OP_COLON,                /**< Begin a colon-def. */
    FF_OP_SEMICOLON,            /**< End a colon-def (emits EXIT or folds tail-NEST). */
    FF_OP_IMMEDIATE,            /**< Mark just-defined word immediate. */
    FF_OP_LBRACKET,             /**< Switch to interpret mode inside a colon-def. */
    FF_OP_RBRACKET,             /**< Resume compile mode. */
    FF_OP_TICK,                 /**< `'` — push xt of next word. */
    FF_OP_BRACKET_TICK,         /**< `[']` — compile-time tick. */
    FF_OP_EXECUTE,              /**< Pop xt, recursively call ff_exec on it. */
    FF_OP_STATE,                /**< Push 0 / true depending on FF_STATE_COMPILING. */
    FF_OP_BRACKET_COMPILE,      /**< `[compile]` — compile next word non-immediate. */
    FF_OP_LITERAL,              /**< Pop, compile a literal of that value. */
    FF_OP_COMPILE,              /**< Compile next inline cell verbatim. */
    FF_OP_DOES,                 /**< `DOES>` — install runtime body for the just-created word. */

    /* --- Control flow (immediate) --- */

    FF_OP_QDUP,                 /**< ( n -- n n | 0 -- 0 ) — dup if non-zero. */
    FF_OP_IF, FF_OP_ELSE, FF_OP_THEN,
    FF_OP_BEGIN, FF_OP_UNTIL, FF_OP_AGAIN,
    FF_OP_WHILE, FF_OP_REPEAT,
    FF_OP_DO, FF_OP_QDO, FF_OP_LOOP, FF_OP_PLOOP,
    FF_OP_QUIT,                 /**< Drop everything and bail to the host loop. */
    FF_OP_ABORT,                /**< Reset the engine state. */
    FF_OP_THROW,                /**< ANS Forth THROW: pop n; if non-zero, unwind to the most recent CATCH. */
    FF_OP_CATCH,                /**< ANS Forth CATCH: execute xt, push 0 on clean return or n on THROW. */
    FF_OP_ABORTQ,               /**< `abort"` — abort with the inline message. */

    /* --- Definitions --- */

    FF_OP_CREATE,               /**< Create an unnamed placeholder; next token names it. */
    FF_OP_FORGET,               /**< Mark the next token to be forgotten. */
    FF_OP_VARIABLE,             /**< Create + reserve one cell. */
    FF_OP_CONSTANT,             /**< Create a word whose runtime pushes a stored value. */
    FF_OP_DEFER,                /**< Create a deferred word (ANS DEFER); next token names it. */
    FF_OP_IS,                   /**< Pop xt and store into next-token-named deferred word (ANS IS). */

    /* --- Heap --- */

    FF_OP_HERE,                 /**< Push a pointer to the next free heap slot. */
    FF_OP_STORE,                /**< ( v a -- ) — store v at address a. */
    FF_OP_FETCH,                /**< ( a -- v ) — fetch from address a. */
    FF_OP_PLUS_STORE,           /**< ( v a -- ) — `*a += v`. */
    FF_OP_ALLOT,                /**< Reserve n cells in the current heap. */
    FF_OP_COMMA,                /**< Append TOS to the heap. */
    FF_OP_C_STORE,              /**< Byte store. */
    FF_OP_C_FETCH,              /**< Byte fetch. */
    FF_OP_C_COMMA,              /**< Append TOS as a single byte. */
    FF_OP_C_ALIGN,              /**< Align current heap to a cell boundary. */

    /* --- Strings --- */

    FF_OP_STRING,               /**< Reserve a string buffer of TOS bytes. */
    FF_OP_S_STORE,              /**< Copy string @ source → dest. */
    FF_OP_S_CAT,                /**< Concatenate strings. */
    FF_OP_STRLEN,               /**< Replace TOS-string with its length. */
    FF_OP_STRCMP,               /**< Pop two strings, push -1/0/+1. */

    /* --- Evaluation --- */

    FF_OP_EVALUATE,             /**< ff_eval() on TOS-string. */
    FF_OP_LOAD,                 /**< ff_load() on TOS-path. */

    /* --- Word-field introspection --- */

    FF_OP_FIND,                 /**< Look up a word by name; push word_ptr or 0. */
    FF_OP_TO_NAME,              /**< Word_ptr → name-cstr. */
    FF_OP_TO_BODY,              /**< Word_ptr → heap.data pointer. */

    /* --- Arrays --- */

    FF_OP_ARRAY,                /**< Reserve N cells under a named word. */

    /* --- File I/O --- */

    FF_OP_SYSTEM, FF_OP_STDIN, FF_OP_STDOUT, FF_OP_STDERR,
    FF_OP_FOPEN, FF_OP_FCLOSE, FF_OP_FGETS, FF_OP_FPUTS,
    FF_OP_FGETC, FF_OP_FPUTC, FF_OP_FTELL, FF_OP_FSEEK,
    FF_OP_SEEK_SET, FF_OP_SEEK_CUR, FF_OP_SEEK_END,
    FF_OP_ERRNO,                /**< Push current C @c errno. */
    FF_OP_STRERROR,             /**< Translate errno on TOS to a message pointer. */

    /* --- Debug --- */

    FF_OP_TRACE,                /**< Toggle FF_STATE_TRACE. */
    FF_OP_BACKTRACE,            /**< Toggle FF_STATE_BACKTRACE. */
    FF_OP_DUMP,                 /**< Hex+ASCII memory dump. */
    FF_OP_MEMSTAT,              /**< Print process memory stats (UNIX only). */

    /* --- Dictionary introspection --- */

    FF_OP_WORDS,                /**< List every word. */
    FF_OP_WORDSUSED,            /**< List words that have been looked up at least once. */
    FF_OP_WORDSUNUSED,          /**< Complement of FF_OP_WORDSUSED. */
    FF_OP_MAN,                  /**< Print manual entry for next-token word. */
    FF_OP_DUMP_WORD,            /**< Print raw heap of next-token word. */
    FF_OP_SEE,                  /**< Decompile next-token word back to Forth syntax. */

    FF_OP_COUNT                 /**< Count of valid opcodes — keep last. */
} ff_opcode_t;
