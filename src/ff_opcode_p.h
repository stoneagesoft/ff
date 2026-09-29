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
 * | @c FF_OP_PRINT_STR           | opcode + string    | Runtime of `."`.                      |
 * | @c FF_OP_ABORTQ_RUNTIME      | opcode + string    | Runtime of `abort"`.                  |
 * | @c FF_OP_LITADD / LITSUB     | opcode + n         | Superinstruction (LIT + arith).       |
 * | @c FF_OP_BRANCH / QBRANCH    | opcode + offset    | Relative jump (negative = backward).  |
 * | @c FF_OP_XDO / XQDO / XLOOP  | opcode + offset    | Counted-loop scaffolding.             |
 * | @c FF_OP_SCOPE_* / ARG       | opcode + operand   | Stack scopes (packed operand).        |
 * | every other opcode           | single cell        | No operand.                           |
 */

#pragma once

/**
 * @def FF_OPCODES
 * @brief Every opcode, in enum order, as `X(name, layout)`.
 *
 * The one list the enum below and the operand-layout table
 * (ff_opcode_meta.c) are generated from, so the two can't drift apart.
 * `layout` names an ff_op_layout_t (NONE, INT, REAL, WORD, FN, STR):
 * what, if anything, follows the opcode in compiled code. A built-in
 * word's name, immediacy and manual are in its registration table
 * (words/ff_words_*.c), its behaviour in its dispatch case
 * (words/ff_words_*_p.h).
 */
#define FF_OPCODES(X) \
    /* --- Structural / control flow --- */ \
    X(CALL,             FN)    /* + fn_ptr — external native word escape hatch. */ \
    X(NEST,             WORD)  /* + word_ptr — colon-def invocation; pushes return frame. */ \
    X(TNEST,            WORD)  /* + word_ptr — tail-call NEST that replaces caller's frame. */ \
    X(EXIT,             NONE)  /* Pop the return frame and resume; a NULL ip there ends ff_exec. */ \
    X(LIT,              INT)   /* + value — push an inline cell. */ \
    X(LIT0,             NONE)  /* Push 0 — specialized for the most common literal. */ \
    X(LIT1,             NONE)  /* Push 1. */ \
    X(LITM1,            NONE)  /* Push -1. */ \
    X(LITADD,           INT)   /* + n — superinstruction: TOS += n. */ \
    X(LITSUB,           INT)   /* + n — superinstruction: TOS -= n. */ \
    X(FLIT,             REAL)  /* + value — push an inline real. */ \
    X(STRLIT,           STR)   /* + skip + bytes — push a pointer to the inline string. */ \
    X(PRINT_STR,        STR)   /* + skip + bytes — print the inline string (runtime of `."`). */ \
    X(ABORTQ_RUNTIME,   STR)   /* + skip + bytes — THROW -2 with the inline message (`abort"`). */ \
    X(BRANCH,           INT)   /* + offset — unconditional jump. */ \
    X(QBRANCH,          INT)   /* + offset — jump if TOS is zero (consumes TOS). */ \
    /* --- Scopes: `{ ( a b -- c ) … }` --- */ \
    X(SCOPE_ENTER,      INT)   /* + packed — install the data-stack barrier (FF_SCOPE_PACK_ENTER). */ \
    X(SCOPE_EXIT,       INT)   /* + packed — check arity, slide outputs over inputs, restore barrier. */ \
    X(SCOPE_UNWIND,     INT)   /* + packed — SCOPE_EXIT ahead of an early `exit` / `leave`. */ \
    X(ARG,              INT)   /* + k — push data[floor - k]: named scope input k (1..nargs). */ \
    /* --- Runtimes for words made with create / does> / constant / array --- */ \
    X(DOES_RUNTIME,     WORD)  /* + word_ptr — DOES>-clause entry. */ \
    X(CREATE_RUNTIME,   WORD)  /* + word_ptr — push word's heap.data pointer. */ \
    X(CONSTANT_RUNTIME, WORD)  /* + word_ptr — push word's heap.data[0]. */ \
    X(ARRAY_RUNTIME,    WORD)  /* + word_ptr — index into word's heap (TOS = base + idx). */ \
    X(DEFER_RUNTIME,    WORD)  /* + word_ptr — call through the xt at heap.data[0] (ANS DEFER). */ \
    X(IS_RUNTIME,       WORD)  /* + word_ptr — pop xt into that deferred word (compiled `is`). */ \
    X(VAR_FETCH,        WORD)  /* + word_ptr — push word's heap.data[0] (peephole `v @`). */ \
    X(VAR_STORE,        WORD)  /* + word_ptr — pop, store at heap.data[0] (peephole `v !`). */ \
    X(VAR_PLUS_STORE,   WORD)  /* + word_ptr — pop, add to heap.data[0] (peephole `v +!`). */ \
    /* --- Stack manipulation --- */ \
    X(DUP,              NONE)  /* ( a -- a a ) */ \
    X(DROP,             NONE)  /* ( a -- ) */ \
    X(SWAP,             NONE)  /* ( a b -- b a ) */ \
    X(OVER,             NONE)  /* ( a b -- a b a ) */ \
    X(ROT,              NONE)  /* ( a b c -- b c a ) */ \
    X(NROT,             NONE)  /* ( a b c -- c a b ) */ \
    X(PICK,             NONE)  /* ( … n -- … item-at-depth-n ) */ \
    X(ROLL,             NONE)  /* Rotate item at depth n to TOS. */ \
    X(DEPTH,            NONE)  /* ( -- n ) push current data-stack depth. */ \
    X(CLEAR,            NONE)  /* Drop every data-stack item. */ \
    X(TO_R,             NONE)  /* ( a -- ) R: ( -- a ) — move TOS to return stack. */ \
    X(FROM_R,           NONE)  /* Inverse of FF_OP_TO_R. */ \
    X(FETCH_R,          NONE)  /* Copy R's TOS to data stack. */ \
    /* --- Double-cell stack ops --- */ \
    X(2DUP,             NONE)  /* ( a b -- a b a b ) */ \
    X(2DROP,            NONE)  /* ( a b -- ) */ \
    X(2SWAP,            NONE)  /* ( a b c d -- c d a b ) */ \
    X(2OVER,            NONE)  /* ( a b c d -- a b c d a b ) */ \
    /* --- Integer math and comparisons --- */ \
    X(ADD,              NONE) \
    X(SUB,              NONE) \
    X(MUL,              NONE) \
    X(DIV,              NONE) \
    X(MOD,              NONE) \
    X(DIVMOD,           NONE) \
    X(MIN,              NONE) \
    X(MAX,              NONE) \
    X(NEGATE,           NONE) \
    X(ABS,              NONE) \
    X(AND,              NONE) \
    X(OR,               NONE) \
    X(XOR,              NONE) \
    X(NOT,              NONE) \
    X(SHIFT,            NONE) \
    X(EQ,               NONE) \
    X(NEQ,              NONE) \
    X(LT,               NONE) \
    X(GT,               NONE) \
    X(LE,               NONE) \
    X(GE,               NONE) \
    X(ZERO_EQ,          NONE) \
    X(ZERO_NEQ,         NONE) \
    X(ZERO_LT,          NONE) \
    X(ZERO_GT,          NONE) \
    X(INC,              NONE) \
    X(DEC,              NONE) \
    X(INC2,             NONE) \
    X(DEC2,             NONE) \
    X(MUL2,             NONE) \
    X(DIV2,             NONE) \
    X(SET_BASE,         NONE)  /* Pop n, set the print/parse base to n (10 or 16). */ \
    /* --- Floating-point --- */ \
    X(FADD,             NONE) \
    X(FSUB,             NONE) \
    X(FMUL,             NONE) \
    X(FDIV,             NONE) \
    X(FNEGATE,          NONE) \
    X(FABS,             NONE) \
    X(FSQRT,            NONE) \
    X(FSIN,             NONE) \
    X(FCOS,             NONE) \
    X(FTAN,             NONE) \
    X(FASIN,            NONE) \
    X(FACOS,            NONE) \
    X(FATAN,            NONE) \
    X(FATAN2,           NONE) \
    X(FEXP,             NONE) \
    X(FLOG,             NONE) \
    X(FPOW,             NONE) \
    X(F_DOT,            NONE)  /* Print top-of-stack as a real (`f.`). */ \
    X(FLOAT,            NONE)  /* Convert TOS int → real bit-pattern. */ \
    X(FIX,              NONE)  /* Convert TOS real → truncated int. */ \
    X(PI,               NONE)  /* Push PI. */ \
    X(E_CONST,          NONE)  /* Push e. */ \
    X(FEQ,              NONE) \
    X(FNEQ,             NONE) \
    X(FLT,              NONE) \
    X(FGT,              NONE) \
    X(FLE,              NONE) \
    X(FGE,              NONE) \
    /* --- Console I/O --- */ \
    X(DOT,              NONE)  /* Print TOS as integer in current base. */ \
    X(QUESTION,         NONE)  /* Print value at the address on TOS. */ \
    X(CR,               NONE)  /* Print newline. */ \
    X(EMIT,             NONE)  /* Print TOS as a single byte. */ \
    X(TYPE,             NONE)  /* Print NUL-terminated string at TOS. */ \
    X(DOT_S,            NONE)  /* Print full data stack as a table. */ \
    X(DOT_PAREN,        NONE)  /* `.(` — print the string that follows at once, even when compiling. */ \
    X(DOTQUOTE,         NONE)  /* `."` — compile a print of the string that follows. */ \
    /* --- Counted loops --- */ \
    X(XDO,              INT)   /* + offset — runtime DO entry. */ \
    X(XQDO,             INT)   /* + offset — runtime ?DO entry (skip body if start==limit). */ \
    X(XLOOP,            INT)   /* + offset — runtime LOOP back-edge. */ \
    X(PXLOOP,           INT)   /* + offset — runtime +LOOP back-edge. */ \
    X(LOOP_I,           NONE)  /* Push current loop index (`i`). */ \
    X(LOOP_J,           NONE)  /* Push outer loop index (`j`). */ \
    X(LEAVE,            NONE)  /* Exit innermost counted loop early. */ \
    X(UNLOOP,           NONE)  /* Drop the innermost loop parameters (ahead of an `exit` from a loop). */ \
    X(I_ADD,            NONE)  /* Superinstruction: i + (add the loop index to TOS). */ \
    X(I_ADD_LOOP,       INT)   /* Superinstruction: i + loop (fused index+ and loop back-edge). */ \
    X(NIP,              NONE)  /* ( a b -- b ) — drop the second-from-top item. */ \
    X(TUCK,             NONE)  /* ( a b -- b a b ) — copy TOS under NOS. */ \
    X(OVER_PLUS,        NONE)  /* Superinstruction: over + (TOS += NOS). */ \
    X(R_PLUS,           NONE)  /* Superinstruction: r@ + (add return-stack TOS to data TOS). */ \
    X(DUP_ADD,          NONE)  /* Superinstruction: dup + (TOS *= 2). */ \
    /* --- Compile-time / immediate --- */ \
    X(COLON,            NONE)  /* Begin a colon-def. */ \
    X(SEMICOLON,        NONE)  /* End a colon-def (emits EXIT or folds tail-NEST). */ \
    X(LBRACE,           NONE)  /* Immediate `{` — open a scope; starts signature collection. */ \
    X(RBRACE,           NONE)  /* Immediate `}` — close a scope; emits FF_OP_SCOPE_EXIT. */ \
    X(IMMEDIATE,        NONE)  /* Mark just-defined word immediate. */ \
    X(LBRACKET,         NONE)  /* Switch to interpret mode inside a colon-def. */ \
    X(RBRACKET,         NONE)  /* Resume compile mode. */ \
    X(TICK,             NONE)  /* `'` — push xt of next word. */ \
    X(BRACKET_TICK,     NONE)  /* `[']` — compile-time tick. */ \
    X(EXECUTE,          NONE)  /* Pop xt, recursively call ff_exec on it. */ \
    X(STATE,            NONE)  /* Push 0 / true depending on FF_STATE_COMPILING. */ \
    X(BRACKET_COMPILE,  NONE)  /* `[compile]` — compile next word non-immediate. */ \
    X(LITERAL,          NONE)  /* Pop, compile a literal of that value. */ \
    X(COMPILE,          NONE)  /* `compile` — parse a word; compile code that compiles a call to it. */ \
    X(POSTPONE,         NONE)  /* `postpone` — parse next word; defer its compilation semantics. */ \
    X(POSTPONE_RUNTIME, WORD)  /* + word_ptr — compile a call to the word into the current definition. */ \
    X(RECURSE,          NONE)  /* `recurse` — compile a call to the definition being compiled. */ \
    X(DOES,             NONE)  /* `DOES>` — install runtime body for the just-created word. */ \
    /* --- Control flow (immediate) --- */ \
    X(QDUP,             NONE)  /* ( n -- n n | 0 -- 0 ) — dup if non-zero. */ \
    X(IF,               NONE) \
    X(ELSE,             NONE) \
    X(THEN,             NONE) \
    X(BEGIN,            NONE) \
    X(UNTIL,            NONE) \
    X(AGAIN,            NONE) \
    X(WHILE,            NONE) \
    X(REPEAT,           NONE) \
    X(DO,               NONE) \
    X(QDO,              NONE) \
    X(LOOP,             NONE) \
    X(PLOOP,            NONE) \
    X(QUIT,             NONE)  /* Drop everything and bail to the host loop. */ \
    X(ABORT,            NONE)  /* Reset the engine state. */ \
    X(THROW,            NONE)  /* ANS THROW: pop n; if non-zero, unwind to the most recent CATCH. */ \
    X(CATCH,            NONE)  /* ANS CATCH: execute xt; push 0, or the THROW code. */ \
    X(ABORTQ,           NONE)  /* `abort"` — compile a -2 THROW with the string that follows. */ \
    /* --- Definitions --- */ \
    X(CREATE,           NONE)  /* Make a data word named by the next token. */ \
    X(FORGET,           NONE)  /* Remove the next-token-named word and every later one. */ \
    X(VARIABLE,         NONE)  /* Create + reserve one cell. */ \
    X(CONSTANT,         NONE)  /* Create a word whose runtime pushes a stored value. */ \
    X(DEFER,            NONE)  /* Create a deferred word (ANS DEFER); next token names it. */ \
    X(IS,               NONE)  /* Pop xt and store into next-token-named deferred word (ANS IS). */ \
    /* --- Heap --- */ \
    X(HERE,             NONE)  /* Push a pointer to the next free heap slot. */ \
    X(STORE,            NONE)  /* ( v a -- ) — store v at address a. */ \
    X(FETCH,            NONE)  /* ( a -- v ) — fetch from address a. */ \
    X(PLUS_STORE,       NONE)  /* ( v a -- ) — `*a += v`. */ \
    X(ALLOT,            NONE)  /* Reserve n cells in the current heap. */ \
    X(COMMA,            NONE)  /* Append TOS to the heap. */ \
    X(C_STORE,          NONE)  /* Byte store. */ \
    X(C_FETCH,          NONE)  /* Byte fetch. */ \
    X(C_COMMA,          NONE)  /* Append TOS as a single byte. */ \
    X(C_ALIGN,          NONE)  /* Align current heap to a cell boundary. */ \
    /* --- Strings --- */ \
    X(STRING,           NONE)  /* Reserve a string buffer of TOS bytes. */ \
    X(S_STORE,          NONE)  /* Copy string @ source → dest. */ \
    X(S_CAT,            NONE)  /* Concatenate strings. */ \
    X(STRLEN,           NONE)  /* Replace TOS-string with its length. */ \
    X(STRCMP,           NONE)  /* Pop two strings, push -1/0/+1. */ \
    /* --- Evaluation --- */ \
    X(EVALUATE,         NONE)  /* ff_eval() on TOS-string. */ \
    X(LOAD,             NONE)  /* ff_load() on TOS-path. */ \
    X(PARSE_WORD,       NONE)  /* `parse-word` — next whitespace token as a C string. */ \
    X(PARSE,            NONE)  /* `parse` — text up to a delimiter char as a C string. */ \
    /* --- Word-field introspection --- */ \
    X(FIND,             NONE)  /* Look up a word by name; push word_ptr or 0. */ \
    X(TO_NAME,          NONE)  /* Word_ptr → name-cstr. */ \
    X(TO_BODY,          NONE)  /* Word_ptr → heap.data pointer. */ \
    /* --- Arrays --- */ \
    X(ARRAY,            NONE)  /* Reserve N cells under a named word. */ \
    /* --- File I/O --- */ \
    X(SYSTEM,           NONE) \
    X(STDIN,            NONE) \
    X(STDOUT,           NONE) \
    X(STDERR,           NONE) \
    X(FOPEN,            NONE) \
    X(FCLOSE,           NONE) \
    X(FGETS,            NONE) \
    X(FPUTS,            NONE) \
    X(FGETC,            NONE) \
    X(FPUTC,            NONE) \
    X(FTELL,            NONE) \
    X(FSEEK,            NONE) \
    X(SEEK_SET,         NONE) \
    X(SEEK_CUR,         NONE) \
    X(SEEK_END,         NONE) \
    X(ERRNO,            NONE)  /* Push current C @c errno. */ \
    X(STRERROR,         NONE)  /* Translate errno on TOS to a message pointer. */ \
    /* --- Debug --- */ \
    X(TRACE,            NONE)  /* Toggle FF_STATE_TRACE. */ \
    X(BACKTRACE,        NONE)  /* Toggle FF_STATE_BACKTRACE. */ \
    X(DUMP,             NONE)  /* Hex+ASCII memory dump. */ \
    X(MEMSTAT,          NONE)  /* Print process memory stats (UNIX only). */ \
    /* --- Dictionary introspection --- */ \
    X(WORDS,            NONE)  /* List every word. */ \
    X(WORDSUSED,        NONE)  /* List words that have been looked up at least once. */ \
    X(WORDSUNUSED,      NONE)  /* Complement of FF_OP_WORDSUSED. */ \
    X(MAN,              NONE)  /* Print manual entry for next-token word. */ \
    X(DUMP_WORD,        NONE)  /* Print raw heap of next-token word. */ \
    X(SEE,              NONE)  /* Decompile next-token word back to Forth syntax. */

/**
 * @enum ff_opcode
 * @brief Identifier carried in each compiled cell.
 *
 * @c FF_OP_NONE is -1 (sentinel) and the regular opcodes, generated from
 * FF_OPCODES, start at 0 so they index a virtual jump table cleanly. The
 * trailing @c FF_OP_COUNT records the count.
 */
typedef enum ff_opcode
{
    FF_OP_NONE = -1,            /**< Sentinel: word has no opcode assigned (e.g. external native). */
#define FF_OP_ENUM_(name, layout) FF_OP_##name,
    FF_OPCODES(FF_OP_ENUM_)
#undef FF_OP_ENUM_
    FF_OP_COUNT                 /**< Count of valid opcodes — keep last. */
} ff_opcode_t;


/* ===================================================================
 * Scope operand packing
 *
 * SCOPE_ENTER / SCOPE_EXIT each carry exactly one inline operand cell,
 * with their fields bit-packed into it. This is deliberate: every
 * opcode in ff is `opcode + at most one cell` (see ff_opcode_meta_p.h),
 * and the tail-call peephole in `;` reads data[size - 2] expecting an
 * opcode. A two-operand encoding would put a small integer there and
 * alias FF_OP_NEST (== 1) for a one-input scope.
 * =================================================================== */

/** @brief Pack a SCOPE_ENTER operand. @p inherit is true for `( ... -- ... )`. */
#define FF_SCOPE_PACK_ENTER(nargs, inherit) \
    ((ff_int_t)(((nargs) & 0xFFFF) | ((inherit) ? (1 << 16) : 0)))

/** @brief Named-input count from a packed SCOPE_ENTER operand. */
#define FF_SCOPE_NARGS(p)       ((int)((p) & 0xFFFF))
/** @brief True when the scope inherits the enclosing barrier (`( ... -- ... )`). */
#define FF_SCOPE_INHERIT(p)     (((p) & (1 << 16)) != 0)

/** @brief Pack a SCOPE_EXIT operand. @p varout is true for `-- ...`. */
#define FF_SCOPE_PACK_EXIT(nargs, nouts, varout) \
    ((ff_int_t)(((nargs) & 0xFFFF) | (((nouts) & 0xFF) << 16) \
                | ((varout) ? (1 << 24) : 0)))

/** @brief Declared output count from a packed SCOPE_EXIT operand. */
#define FF_SCOPE_NOUTS(p)       ((int)(((p) >> 16) & 0xFF))
/** @brief True when the scope declared `-- ...` and skips the arity check. */
#define FF_SCOPE_VAROUT(p)      (((p) & (1 << 24)) != 0)
