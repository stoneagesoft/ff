# Design and Architecture

## Overview

*ff* is a compact, embeddable Forth interpreter written in ISO C17. It is
designed as a library: the caller supplies I/O callbacks through
`ff_platform_t` and drives the interpreter via `ff_eval()`. The
implementation gives particular attention to interpreter throughput
while keeping the source small and comprehensible.

### Big picture

```
                +----------------------------------------------+
                |                ff_platform_t                 |
                |   .vprintf  .vtracef   .context              |
                +----------------------+-----------------------+
                                       |
                          (caller-supplied callbacks)
                                       v
  +--------------------------------------------------------------+
  |                          struct ff                           |
  |                                                              |
  |  +-------------+   +----------------+   +---------------+    |
  |  |  tokenizer  |-->|    ff_eval     |-->|    ff_exec    |    |
  |  | (state mch) |   | (outer interp) |   | (inner interp |    |
  |  +-------------+   +-------+--------+   |  switch loop) |    |
  |                            |            +-------+-------+    |
  |                            v                    |            |
  |                     +------------+              |            |
  |                     |  ff_dict   |<-------------+            |
  |                     | words[]  --|--> ff_word --+            |
  |                     | buckets[]  |   .opcode                 |
  |                     | static_pool|   .heap.data --> [...]    |
  |                     +------------+   .does                   |
  |                                                              |
  |  +------------+   +--------------+   +------------------+    |
  |  | stack[512] |   | r_stack[512] |   | bt_stack[256]    |    |
  |  |  data top  |   |   data top   |   |  word-ptr ring   |    |
  |  +------------+   +--------------+   +------------------+    |
  |                                                              |
  |  +-----------------------------------------------------+     |
  |  |       pad ring  [128 x 256 bytes]   for S" etc.     |     |
  |  +-----------------------------------------------------+     |
  +--------------------------------------------------------------+
```

Arrows are direct pointer references; control flows top-to-bottom
through the boxed pipeline. Every box that carries a backing array
(`stack`, `r_stack`, `bt_stack`, `pad`, each `ff_word`'s `heap.data`)
is a separate allocation — there is no single "memory" region in
*ff*. Sections below take each box in turn.


### Lineage

*ff* is heavily inspired by John Walker's
[Atlast](https://www.fourmilab.ch/atlast/) (Fourmilab, 1990) — an
embeddable Forth derivative originally written to script Autodesk's
applications. From Atlast *ff* takes the core philosophical stance:
the interpreter is a *library* delivered to a host application, not a
standalone language; all I/O flows through caller-supplied callbacks
so the engine never has to know whether it's running in a terminal,
a GUI widget, or a network socket; and the host extends the language
by registering its own native C words rather than by patching the
engine. Where this document calls out a design choice that goes
beyond a straight Forth implementation — the per-word heap, the
switch-based dispatch, the C-string convention, the embedded-friendly
error reporting via `ff_tracef` — Atlast was the starting point that
the choice was negotiated against.

The design draws on three ideas from classical Forth implementation
research:

- **Token-threaded code**: compiled words are arrays of machine-cell-
  sized opcode tokens rather than trees or ASTs. Every built-in is an
  opcode — even `NEST`, `EXIT`, and the `does>`/`create` runtimes.
- **Switch-dispatch with inlined case bodies**: a single
  `switch (*ip++)` walks the bytecode. Each case body is `#include`d
  from a per-category file in `words/`, so the generated code is one
  indirect jump per opcode — matching computed-goto throughput on
  modern GCC/Clang while still building cleanly under MSVC.
- **Register-cached hot path**: the top-of-stack is held in a local
  scalar (`tos`) for the duration of `ff_exec`, and the instruction
  pointer is a local pointer; both can live in registers. Cached values
  are flushed back to the engine struct only at the few opcodes that
  call out to arbitrary C code.


## Source tree layout

The installed header surface is organised in two tiers.

**Public headers** (directly included by library users):

- `ff.h` — engine life-cycle and top-level API: `ff_new`, `ff_eval`,
  `ff_free`, `ff_exec`, `ff_load`, `ff_abort`, `ff_printf`, `ff_tracef`.
- `ff_error.h` — error codes and severity flags.
- `ff_platform.h` — `ff_platform_t` I/O callback struct.

A typical program that just wants to evaluate Forth source includes
only `ff.h` and `ff_platform.h`.

**Internal headers** (suffix `_p.h`, installed but flagged as advanced
internals):

One private header per engine subsystem defines the corresponding
struct and any hot-path inline functions:

| Header | Contents |
|---|---|
| `ff_p.h` | `struct ff`, `ff_s0`–`ff_s5` / `ff_r0`–`ff_r3` accessors, `FF_SL`/`FF_SO`/`FF_RSL`/`FF_RSO`/`FF_COMPILING` validators — umbrella header for custom-word authors |
| `ff_stack_p.h` | `struct ff_stack`, `ff_tos`/`ff_nos`/`ff_sat` pointer accessors, inline push/pop |
| `ff_bt_stack_p.h` | `struct ff_bt_stack`, inline push |
| `ff_dict_p.h` | `struct ff_dict`, dictionary API, built-in word tables |
| `ff_heap_p.h` | `struct ff_heap`, inline `ff_heap_push`/`ff_heap_ensure` |
| `ff_word_p.h` | `struct ff_word`, `ff_word_fn`, constructors |
| `ff_tokenizer_p.h` | `struct ff_tokenizer`, `ff_tokenizer_next` |

Alongside these, the following `_p.h` files hold pure type/enum/macro
definitions (no corresponding struct body):

`ff_types_p.h`, `ff_config_p.h`, `ff_opcode_p.h`, `ff_state_p.h`,
`ff_base_p.h`, `ff_token_p.h`, `ff_tok_state_p.h`, `ff_word_flags_p.h`,
`ff_word_def_p.h`.

**Implementation files** in `src/` (one `.c` per subsystem):

`ff.c`, `ff_dict.c`, `ff_word.c`, `ff_heap.c`, `ff_stack.c`,
`ff_bt_stack.c`, `ff_tokenizer.c`.

To write custom native C words, include `ff_p.h` — it re-exports every
public and private header the engine itself uses. The `_p.h` suffix
reminds callers that these layouts are subject to change across
releases.

The `words/` directory contains sixteen category pairs, each consisting
of a registration table file and a private dispatch header:

| Category | Registrations (`*.c`) | Opcode case bodies (`*_p.h`) |
|---|---|---|
| array | `ff_words_array.c` | `ff_words_array_p.h` |
| compiler | `ff_words_comp.c` | `ff_words_comp_p.h` |
| console I/O | `ff_words_conio.c` | `ff_words_conio_p.h` |
| control flow | `ff_words_ctrl.c` | `ff_words_ctrl_p.h` |
| debug | `ff_words_debug.c` | `ff_words_debug_p.h` |
| dictionary | `ff_words_dict.c` | `ff_words_dict_p.h` |
| eval | `ff_words_eval.c` | `ff_words_eval_p.h` |
| field | `ff_words_field.c` | `ff_words_field_p.h` |
| file | `ff_words_file.c` | `ff_words_file_p.h` |
| heap | `ff_words_heap.c` | `ff_words_heap_p.h` |
| integer math | `ff_words_math.c` | `ff_words_math_p.h` |
| real math | `ff_words_real.c` | `ff_words_real_p.h` |
| stack (1-cell) | `ff_words_stack.c` | `ff_words_stack_p.h` |
| stack (2-cell) | `ff_words_stack2.c` | `ff_words_stack2_p.h` |
| string | `ff_words_string.c` | `ff_words_string_p.h` |
| variables | `ff_words_var.c` | `ff_words_var_p.h` |

The `*_p.h` headers are not stand-alone — each holds `case FF_OP_XXX:`
clauses that are `#include`d directly inside the dispatch switch in
`ff.c`. The corresponding `*.c` file holds the registration table that
maps Forth names to opcodes.


## Cell model

The fundamental unit of storage is `ff_int_t`, defined in `ff_types.h`:

~~~{.c}
#ifdef FF_32BIT
typedef int32_t  ff_int_t;
typedef float    ff_real_t;
#else
typedef int64_t  ff_int_t;
typedef double   ff_real_t;
#endif
~~~

The default build is 64-bit. Defining `FF_32BIT` at compile time switches
to 32-bit integers and single-precision floats, which can be useful on
embedded targets.

Stacks and compiled-word heaps are both arrays of `ff_int_t`. Floating-point
values, pointers, and function pointers are all stored as `ff_int_t` via
`memcpy` or a cast through `intptr_t`. This keeps the stacks uniform and
avoids alignment complications.

Boolean results follow the Forth convention:

~~~{.c}
enum { FF_TRUE = -1, FF_FALSE = 0 };
~~~

All-ones (`-1`) allows flag results to double as bitmasks in bitwise
operations.


## Engine struct

`struct ff` (defined in `ff_p.h`) holds all mutable state for one
interpreter instance. There is no global state; multiple independent
instances can coexist in the same process.

~~~{.c}
struct ff
{
    ff_platform_t platform;              /* I/O callbacks */

    ff_state_t    state;                 /* mode flags (bitmask) */
    ff_error_t    error;                 /* last error code */
    char          error_msg[FF_ERROR_MSG_SIZE];
    int           error_line;
    int           error_pos;

    ff_dict_t     dict;                  /* word dictionary */

    ff_stack_t    stack;                 /* data stack */
    ff_stack_t    r_stack;               /* return stack */
    ff_bt_stack_t bt_stack;              /* backtrace stack */
    ff_int_t     *ip;                    /* instruction pointer */

    char          pad[FF_PAD_COUNT][FF_PAD_SIZE];  /* temp string ring */
    int           pad_i;

    ff_tokenizer_t tokenizer;
    const char    *input;
    int            input_pos;

    ff_base_t     base;
    ff_word_t    *cur_word;              /* word currently executing */
};
~~~

The `ip` field points into a word's compiled heap and is the program
counter of the inner interpreter. While `ff_exec` is running the live
value is held in a local; the struct field is updated only on calls
out to external C words. A NULL return-stack entry serves as the
sentinel that causes `ff_exec` to return.

`pad` is a fixed-size ring buffer of temporary string slots used for
runtime string manipulation (e.g. the `S"` word). The ring advances one
slot per allocation, providing simple lifetime management without heap
allocation.


## State flags

`ff_state_t` (in `ff_state.h`) is a bitmask that governs the current
interpreter mode:

| Flag | Meaning |
|---|---|
| `FF_STATE_COMPILING` | Building a colon-definition |
| `FF_STATE_DEF_PENDING` | Next token becomes the new word's name |
| `FF_STATE_FORGET_PENDING` | Next token is a word name to delete |
| `FF_STATE_TICK_PENDING` | Next token is pushed as a word address (`'`) |
| `FF_STATE_CTICK_PENDING` | Compile-time `[']` pending |
| `FF_STATE_CBRACK_PENDING` | Inside `[…]` — temporary interpret mode |
| `FF_STATE_STRLIT_ANTIC` | Next string token is a literal (for `."`, `.(`) |
| `FF_STATE_TRACE` | Print each word name before executing it |
| `FF_STATE_BACKTRACE` | Maintain a call-chain for debugging |
| `FF_STATE_BROKEN` | Execution halted; propagates through `ff_exec` callers |
| `FF_STATE_ABORTED` | User abort — resets all state |
| `FF_STATE_ERROR` | Sticky error flag; cleared by `ff_eval` on next call |


## Tokenizer

`ff_tokenizer_next(t, src, &pos)` consumes one token from the null-
terminated C string `src` starting at byte offset `*pos`. It advances
`*pos` past the token and returns one of:

| Token | Value extracted |
|---|---|
| `FF_TOKEN_NULL` | end of input |
| `FF_TOKEN_WORD` | identifier in `t->token` / `t->token_len` |
| `FF_TOKEN_INTEGER` | `t->integer_val` (ff_int_t); hex with `0x` prefix |
| `FF_TOKEN_REAL` | `t->real_val` (ff_real_t) |
| `FF_TOKEN_STRING` | string content in `t->token`, length in `t->token_len` |

The tokenizer handles nested parenthesis comments, tracks line and
character position for error reporting, and understands the full set of
C-style backslash escape sequences plus Unicode escapes (`\uXXXX`,
`\UXXXXXXXX`, `\xXX`). Unicode encoding is delegated to
`utf8catcodepoint()` from the vendored `3rdparty/utf8/utf8.h` library.

Number parsing attempts integer first, then real. Words that cannot be
parsed as numbers are returned as `FF_TOKEN_WORD`.


## Eval loop

`ff_eval(ff, src)` is the outer interpreter. It tokenizes the input string
in a loop and dispatches on the current state:

**Interpret mode** (default): each word is looked up and executed
immediately via `ff_exec`; integers and reals are pushed to the data stack.

**Compile mode** (`FF_STATE_COMPILING` set by `:`): words are compiled
into the heap of the top dictionary entry rather than executed, unless
the word carries `FF_WORD_IMMEDIATE`, in which case it executes at
compile time. Integers are compiled as one of the specialised
literals (`FF_OP_LIT0`/`LIT1`/`LITM1`) when possible, otherwise as
`FF_OP_LIT` + value; reals always go through `FF_OP_FLIT` + bit-cast
value.

The `[` word temporarily clears `FF_STATE_COMPILING`, allowing interpret-
mode evaluation inside a definition. `]` restores compile mode.
`['word']` compiles a word's address as a literal.

Several single-token look-ahead effects are implemented through pending
flags: setting `FF_STATE_TICK_PENDING` before returning from the current
token causes the *next* token's name to be resolved to a word address
instead of executed.

After `ff_exec` returns, `pos` is refreshed from `ff->input_pos` because
a word may have consumed additional input tokens (e.g. `."` reads the
following string literal directly).


## Word structure

~~~{.c}
struct ff_word
{
    char             *name;        /* null-terminated; strdup'd or aliased literal */
    ff_opcode_t       opcode;      /* assigned opcode, or FF_OP_NONE */
    ff_word_flags_t   flags;       /* IMMEDIATE, USED, HIDDEN, STATIC, NATIVE */
    ff_int_t         *does;        /* DOES> clause start (NULL if none) */
    ff_heap_t         heap;        /* compiled body, or [fn_ptr] for natives */
    const char       *manual;      /* help text string (may be NULL) */
    const char       *man_desc;    /* pointer past first '\n' in manual */
    struct ff_word   *next_bucket; /* dict hash chain (newest first) */
};
~~~

There is no `code` field. Every word is dispatched through its
`opcode`:

- **Built-in words** carry a real opcode (`FF_OP_DUP`, `FF_OP_ADD`,
  `FF_OP_NEST`, `FF_OP_DOES_RUNTIME`, …) and the case body lives in
  `words/ff_words_*_p.h`. Compiling such a word emits one cell for the
  opcode plus zero or one inline operand.

- **External native words** added by an embedder use the `FF_OP_CALL`
  escape hatch. They carry `opcode = FF_OP_NONE`, the `FF_WORD_NATIVE`
  flag, and stash their `void (*)(ff_t *)` function pointer in
  `heap.data[0]`. A compiled caller emits the two-cell sequence
  `FF_OP_CALL`, `fn_ptr`.

- **Colon-definitions** carry `FF_OP_NEST` (or `FF_OP_TNEST` after
  tail-call peephole optimisation) and a real bytecode body in `heap`.

- **`create`/`does>`/`constant`/`array` words** carry one of
  `FF_OP_CREATE_RUNTIME`, `FF_OP_DOES_RUNTIME`,
  `FF_OP_CONSTANT_RUNTIME`, `FF_OP_ARRAY_RUNTIME`, all of which read
  the word's `heap.data` (and `does` for `FF_OP_DOES_RUNTIME`).

`flags` carries OR-able bits including `FF_WORD_IMMEDIATE` (compile-
time word), `FF_WORD_USED` (set on lookup; reported by `wordsunused`),
`FF_WORD_HIDDEN` (omit from `words` listing), `FF_WORD_NATIVE` (call
through `heap.data[0]`), and `FF_WORD_STATIC` (struct + name belong to
the dict's static pool — `ff_word_free` skips the alloc).


## Heap and compilation

Standard Forth has a single contiguous heap — one global `HERE`
pointer, every new word's body appended to the running tail. *ff*
deliberately departs from that: there is no global heap and no global
`HERE`. Each word owns its own `ff_heap_t`, an independently
`malloc`'d cell array:

~~~{.c}
struct ff_heap
{
    ff_int_t *data;       /* dynamically grown cell array */
    size_t    size;       /* used cells */
    size_t    capacity;   /* allocated cells */
    uint8_t   byte_off;   /* sub-cell byte offset (for string packing) */
};
~~~

The `here` word in this design returns a pointer to the next free
slot of the *currently-defining* word's heap, not a position in any
global region. Compile-time helpers like `,`, `c,`, and `allot` write
into that same per-word heap.

This decision shapes several other parts of the engine:

- **In-place growth doesn't move addresses.** A word's heap can
  `realloc` itself larger as the body extends (`,`, `allot`,
  post-`;` `does>`-clause attachment) without disturbing the addresses
  baked into any other word's compiled bytecode. A contiguous heap
  could not grow without invalidating every cross-reference baked
  during earlier compilations.

- **`forget` releases memory cleanly.** When `forget name` cascades
  through everything defined after `name`, each removed word's heap
  is `free()`d as a single allocation — there is no compaction step,
  no global high-water mark to roll back. Forget cascades for the
  same reason it does in standard Forth (later words may have baked
  in addresses pointing at earlier words' bytecode entry points), but
  the implementation is straight pointer-and-free arithmetic rather
  than heap surgery.

- **Variables, constants, and arrays each carry their own storage.**
  A `variable v` next to `: foo … ;` doesn't fragment a shared data
  area; `v`'s storage is the first cell of its own `heap.data`,
  reachable through `FF_OP_CREATE_RUNTIME` which simply pushes
  `&heap.data[0]`. Code and data don't compete for the same address
  range.

- **Future selective forget is at most a flag away.** Removing the
  contiguous-heap invariant means a word with no inbound bytecode
  references *could* be removed in isolation — the data structure
  supports it; the policy is currently the conservative cascade for
  consistency with classical Forth semantics.

`ff_heap_compile_word(heap, w)` appends the calling sequence for word `w`:

- **Built-in opcode (no operand)** — e.g. `FF_OP_DUP`, `FF_OP_ADD`:
  single cell = `w->opcode`.
- **Opcode that takes a `ff_word_t *` operand** — `FF_OP_NEST`,
  `FF_OP_TNEST`, `FF_OP_DOES_RUNTIME`, `FF_OP_CREATE_RUNTIME`,
  `FF_OP_CONSTANT_RUNTIME`, `FF_OP_ARRAY_RUNTIME`: two cells —
  `w->opcode`, `(ff_int_t)(intptr_t)w`.
- **External native (`FF_OP_NONE`)**: two cells — `FF_OP_CALL`,
  `(ff_int_t)(intptr_t)ff_word_native_fn(w)`.

Other compile helpers:

| Helper | Emits |
|---|---|
| `ff_heap_compile_int(h, v)` | single cell = `v` |
| `ff_heap_compile_real(h, r)` | single cell = `r` bit-cast to `ff_int_t` |
| `ff_heap_compile_str(h, s, len)` | length cell + string bytes packed into cells |

String data is packed byte-by-byte into cells using `byte_off`, so an
`n`-byte string occupies `ceil((n+1) / sizeof(ff_int_t)) + 1` cells
(the first cell holds a skip count). `ff_heap_align()` resets `byte_off`
to zero before the next token.

The bytes themselves are stored as a **NUL-terminated C string**, not
as a Forth counted string. Standard Forth represents a string either
as a counted string (the first byte holds the length, content
follows) or as a `( c-addr u )` pair on the stack (pointer plus
explicit length cell). *ff* uses neither: every string-valued word
returns or accepts a single `char *` pointer to a NUL-terminated
sequence of bytes, exactly as in C. The skip count in the bytecode
exists only so the inner interpreter knows where the string ends and
the next opcode begins; it is *not* the string's length and is never
exposed to Forth code.

The practical consequences:

- `s!`, `s+`, `strlen`, `strcmp` are direct one-line wrappers around
  `strcpy`, `strcat`, `strlen`, `strcmp`. No length-decoding glue.
- C code that calls `ff_eval` or pulls strings off the stack receives
  ready-to-use `char *` pointers; no helper to convert from a Forth
  counted-string is needed.
- The trade-off is the standard C-string one: getting the length is
  O(n) (a `strlen` walk) rather than O(1) (read the count cell).
  In typical Forth code this happens rarely and the simplicity gain
  outweighs the per-call cost.

**Example — `: square dup * ;`**

~~~
heap.data:
  [0]  FF_OP_DUP
  [1]  FF_OP_MUL
  [2]  FF_OP_EXIT
~~~

**Example — `: greet ." Hello" cr ;`**

~~~
heap.data:
  [0]  FF_OP_STRLIT
  [1]  4              ← skip count (cells to advance past string)
  [2..4]  "Hello\0"  ← string bytes packed into 3 × 8-byte cells
  [5]  FF_OP_CR
  [6]  FF_OP_EXIT
~~~

(`cr` is a built-in opcode so the call collapses to one cell. An
embedder-supplied native would be the two-cell `FF_OP_CALL`, `fn_ptr`
sequence instead.)


## Opcode set

Every built-in word — including the structural ones, the `does>` /
`create` / `constant` / `array` runtimes, and every immediate compiler
word — has a dedicated opcode. The full set has grown to roughly 130
entries (the canonical list is `FF_OP_*` in
[ff_opcode_p.h](src/ff_opcode_p.h)), grouped as follows:

| Group | Examples |
|---|---|
| Structural | `FF_OP_CALL`, `FF_OP_NEST`, `FF_OP_TNEST`, `FF_OP_EXIT`, `FF_OP_BRANCH`, `FF_OP_QBRANCH` |
| Literals | `FF_OP_LIT`, `FF_OP_LIT0`, `FF_OP_LIT1`, `FF_OP_LITM1`, `FF_OP_LITADD`, `FF_OP_LITSUB`, `FF_OP_FLIT`, `FF_OP_STRLIT` |
| Defining-word runtimes | `FF_OP_CREATE_RUNTIME`, `FF_OP_DOES_RUNTIME`, `FF_OP_CONSTANT_RUNTIME`, `FF_OP_ARRAY_RUNTIME`, `FF_OP_DEFER_RUNTIME` |
| Stack manipulation | `FF_OP_DUP`, `FF_OP_DROP`, `FF_OP_SWAP`, `FF_OP_OVER`, `FF_OP_ROT`, `FF_OP_NROT`, `FF_OP_PICK`, `FF_OP_ROLL`, `FF_OP_DEPTH`, `FF_OP_CLEAR`, `FF_OP_TO_R`, `FF_OP_FROM_R`, `FF_OP_FETCH_R` |
| Two-cell stack ops | `FF_OP_2DUP`, `FF_OP_2DROP`, `FF_OP_2SWAP`, `FF_OP_2OVER` |
| Integer math / bitwise / compare | `FF_OP_ADD`/`SUB`/`MUL`/`DIV`/`MOD`/`DIVMOD`, `FF_OP_MIN`/`MAX`/`NEGATE`/`ABS`, `FF_OP_AND`/`OR`/`XOR`/`NOT`/`SHIFT`, `FF_OP_EQ`/`NEQ`/`LT`/`GT`/`LE`/`GE`, `FF_OP_ZERO_EQ`/`ZERO_NEQ`/`ZERO_LT`/`ZERO_GT`, `FF_OP_INC`/`DEC`/`INC2`/`DEC2`/`MUL2`/`DIV2`, `FF_OP_SET_BASE` |
| Floating-point | `FF_OP_FADD`/`FSUB`/`FMUL`/`FDIV`, `FF_OP_FNEGATE`/`FABS`/`FSQRT`, `FF_OP_FSIN`/`FCOS`/`FTAN`/`FASIN`/`FACOS`/`FATAN`/`FATAN2`, `FF_OP_FEXP`/`FLOG`/`FPOW`, `FF_OP_F_DOT`/`FLOAT`/`FIX`/`PI`/`E_CONST`, `FF_OP_FEQ`/`FNEQ`/`FLT`/`FGT`/`FLE`/`FGE` |
| Console I/O | `FF_OP_DOT`, `FF_OP_QUESTION`, `FF_OP_CR`, `FF_OP_EMIT`, `FF_OP_TYPE`, `FF_OP_DOT_S`, `FF_OP_DOT_PAREN`, `FF_OP_DOTQUOTE` |
| Counted loops | `FF_OP_XDO`, `FF_OP_XQDO`, `FF_OP_XLOOP`, `FF_OP_PXLOOP`, `FF_OP_LOOP_I`, `FF_OP_LOOP_J`, `FF_OP_LEAVE`, `FF_OP_I_ADD` (peephole `i +`) |
| Compiler / immediate | `FF_OP_COLON`, `FF_OP_SEMICOLON`, `FF_OP_IMMEDIATE`, `FF_OP_LBRACKET`, `FF_OP_RBRACKET`, `FF_OP_TICK`, `FF_OP_BRACKET_TICK`, `FF_OP_EXECUTE`, `FF_OP_STATE`, `FF_OP_BRACKET_COMPILE`, `FF_OP_LITERAL`, `FF_OP_COMPILE`, `FF_OP_DOES` |
| Control flow | `FF_OP_QDUP`, `FF_OP_IF`/`ELSE`/`THEN`, `FF_OP_BEGIN`/`UNTIL`/`AGAIN`, `FF_OP_WHILE`/`REPEAT`, `FF_OP_DO`/`QDO`/`LOOP`/`PLOOP`, `FF_OP_QUIT`, `FF_OP_ABORT`, `FF_OP_ABORTQ`, `FF_OP_THROW`, `FF_OP_CATCH` |
| Definitions | `FF_OP_CREATE`, `FF_OP_FORGET`, `FF_OP_VARIABLE`, `FF_OP_CONSTANT`, `FF_OP_ARRAY`, `FF_OP_DEFER`, `FF_OP_IS` |
| Heap | `FF_OP_HERE`, `FF_OP_STORE`/`FETCH`/`PLUS_STORE`, `FF_OP_ALLOT`/`COMMA`, `FF_OP_C_STORE`/`C_FETCH`/`C_COMMA`/`C_ALIGN` |
| Strings | `FF_OP_STRING`, `FF_OP_S_STORE`, `FF_OP_S_CAT`, `FF_OP_STRLEN`, `FF_OP_STRCMP` |
| Evaluation | `FF_OP_EVALUATE`, `FF_OP_LOAD` |
| Word-field introspection | `FF_OP_FIND`, `FF_OP_TO_NAME`, `FF_OP_TO_BODY` |
| File I/O | `FF_OP_SYSTEM`, `FF_OP_STDIN`/`STDOUT`/`STDERR`, `FF_OP_FOPEN`/`FCLOSE`/`FGETS`/`FPUTS`/`FGETC`/`FPUTC`/`FTELL`/`FSEEK`, `FF_OP_SEEK_SET`/`SEEK_CUR`/`SEEK_END`, `FF_OP_ERRNO`/`STRERROR` |
| Debug | `FF_OP_TRACE`, `FF_OP_BACKTRACE`, `FF_OP_DUMP`, `FF_OP_MEMSTAT` |
| Dictionary introspection | `FF_OP_WORDS`, `FF_OP_WORDSUSED`, `FF_OP_WORDSUNUSED`, `FF_OP_MAN`, `FF_OP_DUMP_WORD`, `FF_OP_SEE` |

`FF_OP_NONE = -1` is the sentinel used by external native words; it
never appears in compiled bytecode (the compiler emits `FF_OP_CALL`
plus the function pointer instead). `FF_OP_COUNT` is kept last as the
table size.

Specialised literal opcodes (`FF_OP_LIT0`/`LIT1`/`LITM1` push 0/1/-1
without an inline operand; `FF_OP_LITADD`/`LITSUB` fold a `LIT n ADD`
sequence into a single instruction) and the doubled-up arithmetic
forms (`FF_OP_INC2`/`DEC2`/`MUL2`/`DIV2` apply ±1/×2/÷2 directly to
TOS) are emitted by the compiler's peephole pass — see
`words/ff_words_comp.c`.


## Inner interpreter

`ff_exec(ff, w)` runs a single word to completion. It is the hot path
of the interpreter and contains all performance-critical code.

To start a word, `ff_exec` constructs a tiny scratch program rather
than calling out through a function pointer. For an opcoded word
(any built-in, including `NEST`, `DOES_RUNTIME`, `CREATE_RUNTIME`, …)
it builds `[opcode (word_ptr?) FF_OP_EXIT]` in a small local array and
points `ip` at it; for an external native it dispatches the function
directly. A `NULL` return-stack sentinel terminates the run when the
final `EXIT` pops it:

~~~{.c}
bool ff_exec(ff_t *ff, ff_word_t *w)
{
    ff_stack_t    *S  = &ff->stack;
    ff_stack_t    *R  = &ff->r_stack;
    ff_bt_stack_t *BT = &ff->bt_stack;

    ff->cur_word = w;
    int bt_size = BT->top;

    /* …trace / backtrace gating elided… */

    ff_int_t exec_scratch[3];
    ff_int_t *ip;

    if (w->opcode != FF_OP_NONE)
    {
        exec_scratch[0] = w->opcode;
        int n = 1;
        if (opcode_takes_word_operand(w->opcode))
            exec_scratch[n++] = (ff_int_t)(intptr_t)w;
        exec_scratch[n] = FF_OP_EXIT;
        ff_stack_push(R, 0);          /* NULL return sentinel */
        ip = exec_scratch;
    }
    else if (w->flags & FF_WORD_NATIVE)
    {
        ff_word_native_fn(w)(ff);
        ip = ff->ip;
    }

    ff_int_t tos = S->top ? S->data[S->top - 1] : 0;

    for (;;) switch (*ip++)
    {
        case FF_OP_CALL:
            {
                ff_word_fn fn = (ff_word_fn)(intptr_t)*ip++;
                _FF_SYNC();
                fn(ff);
                _FF_RESTORE();
            }
            if (!ip) goto done;
            break;

        #include "ff_words_stack_p.h"
        #include "ff_words_stack2_p.h"
        #include "ff_words_math_p.h"
        #include "ff_words_ctrl_p.h"
        #include "ff_words_real_p.h"
        /* …twelve more category headers… */

        default:
            FF_UNREACHABLE();
    }
}
~~~

The case bodies live in `words/ff_words_*_p.h`. They reference shared
macros (`_FF_SL`/`_FF_SO` validators, `_FF_SYNC`/`_FF_RESTORE` for
calls out to C, `_TOS`/`_NOS`/`_PUSH`/`_DROP` for stack access against
the cached top-of-stack) and shared locals (`S`, `R`, `BT`, `ip`,
`tos`, `ff`) that are in scope at the point of inclusion. Using
`#include` rather than computed-goto keeps the source portable across
GCC, Clang and MSVC while still giving the compiler enough visibility
to compile the switch to a jump table — one indirect branch per
opcode, branch-target predicted per case.


## Performance optimisations

The inner interpreter applies a layered set of optimisations.


### Fixed-array stacks with inline operations

Both stacks are fixed-size arrays inside `ff_t`:

~~~{.c}
struct ff_stack
{
    ff_int_t data[FF_STACK_SIZE];   /* 512 elements */
    size_t   top;
};
~~~

There is no heap allocation at runtime. All push/pop operations are
`static inline` functions in `ff_stack_p.h`, so they compile to a single
store or load with an index increment — no function-call overhead, and
the compiler can keep `S->top` in a register across a basic block. The
private header is installed, so a custom C word that manipulates the
stack gets the same inlined push/pop as the engine's own opcode handlers.

~~~{.c}
static inline void ff_stack_push(ff_stack_t *s, ff_int_t v)
{
    assert(s->top < FF_STACK_SIZE);
    s->data[s->top++] = v;
}

static inline ff_int_t ff_stack_pop(ff_stack_t *s)
{
    assert(s->top > 0);
    return s->data[--s->top];
}
~~~

Three inline functions give zero-cost pointer access to the
top-of-stack, next-on-stack, and arbitrary depth without changing `top`:

~~~{.c}
static inline ff_int_t *ff_tos(ff_stack_t *s)
{
    return &s->data[s->top - 1];
}

static inline ff_int_t *ff_nos(ff_stack_t *s)
{
    return &s->data[s->top - 2];
}

static inline ff_int_t *ff_sat(ff_stack_t *s, size_t i)
{
    return &s->data[s->top - 1 - i];
}
~~~

These are pointer-returning so a call produces an lvalue when dereferenced
— `*ff_tos(S) = v` writes the top, `*ff_tos(S)` reads it. At `-O1` or
higher the compiler inlines the call and folds away the pointer, emitting
the same load/store as a direct array index. The engine-level accessors
`ff_s0`–`ff_s5` and `ff_r0`–`ff_r3` in `ff_p.h` compose these against
`ff->stack` and `ff->r_stack` respectively.

Many opcode handlers manipulate `top` directly after modifying cell
values through these accessors, avoiding a push+pop round-trip:

~~~{.c}
do_add:
    *ff_nos(S) += *ff_tos(S);
    S->top--;
    NEXT();
~~~


### Switch dispatch with inlined cases

The dispatch loop is a single `switch (*ip++)` whose case bodies are
`#include`d from per-category headers. On modern GCC and Clang this
compiles to a jump-table indirect branch with one branch site per
case, which is functionally equivalent to GCC's labels-as-values
computed-goto trick: the branch-target predictor sees a recurring
pattern at each opcode handler rather than at one mega-site. The
switch form has the additional virtue of building cleanly under MSVC,
which has no labels-as-values extension.

The `default:` arm is marked `FF_UNREACHABLE()`, so on GCC/Clang the
compiler can elide bounds checks on the dispatch table.


### Instruction-pointer register caching

In the obvious implementation every opcode handler reads from and writes
to `ff->ip`. That means a load from the engine struct, an operation, and
a store back — through a pointer that the compiler cannot generally hoist
because other pointers may alias the struct.

*ff* caches `ip` in a local pointer for the duration of `ff_exec`:

~~~{.c}
ff_int_t *ip = /* …initialised from exec_scratch or ff->ip… */;

#define _FF_SYNC()    do { ff->ip = ip; _SYNC_TOS(); } while (0)
#define _FF_RESTORE() do { ip = ff->ip; _LOAD_TOS(); } while (0)
~~~

The compiler can allocate `ip` to a hardware register. Each opcode's
`*ip++` then advances a register, not a memory location. `_FF_SYNC()`
flushes the register back to the struct before any opcode body that
calls out to arbitrary C — primarily `FF_OP_CALL` (external natives)
and the few opcodes that re-enter `ff_eval` or `ff_load` — and
`_FF_RESTORE()` reloads on return.


### Top-of-stack register caching

The same register-caching trick is applied a second time, to the top
of the data stack. While `ff_exec` runs, the cell at index `S->top - 1`
is treated as scratch; the live value lives in a local
scalar `tos`:

~~~{.c}
ff_int_t tos = S->top ? S->data[S->top - 1] : 0;

#define _SYNC_TOS()  do { if (S->top) S->data[S->top - 1] = tos; } while (0)
#define _LOAD_TOS()  do { if (S->top) tos = S->data[S->top - 1]; } while (0)

#define _TOS         tos
#define _NOS         (S->data[S->top - 2])
#define _PUSH(x)     do { if (S->top) S->data[S->top - 1] = tos; \
                          tos = (x); ++S->top; } while (0)
#define _DROP()      do { if (--S->top) tos = S->data[S->top - 1]; } while (0)
~~~

Compute-heavy opcode bodies operate entirely on `tos` and `_NOS` and
need no memory traffic for TOS at all. `_FF_SYNC` flushes `tos` to
memory alongside `ip` before every call out to C; `_FF_RESTORE`
reloads it after.


### NEST as an opcode

In an earlier design, colon-definitions were entered by an indirect
call through `w->code = ff_w_nest`. The current design folds that
function into `case FF_OP_NEST:` directly: the compiler emits one
unconditional code path that pushes the return address and assigns
`ip = nw->heap.data`. There is no fast/slow split, no nest-code
pointer comparison, and no separate `ff_w_nest` C function — the nest
body is a handful of inlined instructions inside the dispatch switch.

The `;` (semicolon) compiler peephole observes when a colon-def ends
with `… NEST x EXIT` and rewrites the trailing `NEST` as `FF_OP_TNEST`,
a tail-call variant that re-uses the current return frame instead of
pushing a new one. Deeply chained tail calls cost no extra return-stack
slots.


### Specialised literal opcodes

A naive implementation uses `FF_OP_LIT` for every literal push: an
opcode plus an inline cell. Three patterns are common enough to
warrant their own opcodes:

~~~{.c}
case FF_OP_LIT0:   _PUSH(0);           break;
case FF_OP_LIT1:   _PUSH(1);           break;
case FF_OP_LITM1:  _PUSH(-1);          break;
~~~

Each saves the inline-cell load — important because `0`, `1`, and `-1`
are by far the most common literals in real Forth code. Two further
super-instructions fold a literal-and-add pattern:

~~~{.c}
case FF_OP_LITADD: tos +=  *ip++;      break;   /* LIT n  ADD */
case FF_OP_LITSUB: tos -= -*ip++;      break;   /* LIT n  SUB */
~~~

The `;` compiler emits these forms whenever the source matches.


### BROKEN check placement

`FF_STATE_BROKEN` is set when a fatal error occurs inside a running word.
An early implementation checked this flag at every backward branch
(`do_branch`, `do_xloop`, `do_pxloop`) to ensure the interpreter would not
loop forever after an error. In practice, every execution path through
which `FF_STATE_BROKEN` can be set ultimately reaches `do_exit`, so a
single check there is both necessary and sufficient:

~~~{.c}
do_exit:
    ip = (ff_int_t *)(intptr_t)*ff_tos(R);
    R->top--;
    if (!ip)
        goto done;
    if (ff->state & FF_STATE_BROKEN)
        goto broken;
    NEXT();
~~~

Removing the redundant checks from the branch and loop opcodes eliminates
three conditional branches from the hot path.


### Unified trace gate

In debug builds both `FF_STATE_TRACE` and `FF_STATE_BACKTRACE` need to be
checked at word entry. Two separate `if` statements require two memory
reads of the same `ff->state` field. They are combined behind a single
gate that tests both bits at once:

~~~{.c}
if (ff->state & (FF_STATE_TRACE | FF_STATE_BACKTRACE))
{
    if (ff->state & FF_STATE_TRACE)
        ff_tracef(ff, FF_SEV_TRACE, "%s →", w->name);
    if (ff->state & FF_STATE_BACKTRACE)
        ff_bt_stack_push(BT, w);
}
~~~

In the normal case (no debugging) the whole block is bypassed with one
branch.


## Dictionary

The dictionary couples an ordered list of words with a power-of-two
hash index for O(1) name lookup:

~~~{.c}
struct ff_dict
{
    /* Ordered array (newest at end). Used by ff_dict_top, ff_dict_forget,
       and introspection (`words`, `see`, …). */
    ff_word_t **words;
    size_t      count;
    size_t      capacity;        /* grows by doubling */

    /* Hash buckets: each bucket is a singly-linked list (newest first)
       threaded through ff_word::next_bucket. */
    ff_word_t **buckets;
    size_t      bucket_count;    /* power of two — masking replaces modulo */

    /* Static pool: one big calloc holding all built-in word structs.
       Each entry carries FF_WORD_STATIC so ff_word_free skips it; the
       block is freed wholesale by ff_dict_destroy. Replaces what used
       to be ~150 separate mallocs at startup. */
    ff_word_t  *static_pool;
    size_t      static_pool_size;
};
~~~

**Lookup** hashes the (case-insensitive) name into a bucket and walks
the bucket list newest-first, so a newly defined word shadows an older
one with the same name. On a hit the word's `FF_WORD_USED` flag is set
— `wordsunused` consults it to report dead definitions.

**Append** (`ff_dict_append`) puts the word at the end of `words` and
links it at the head of its hash bucket.

**Forget** removes the named word and every word defined after it,
truncating `words` and rebuilding `buckets` from scratch.

The static pool is filled at init time by `ff_dict_define_words`,
which walks the per-category `FF_*_WORDS` registration tables and
stamps the corresponding pool slots. Built-in word names are taken by
reference from the def-table string literals (no `strdup`).


## Return stack

The return stack (`ff->r_stack`) is a second `ff_stack_t` that serves
three purposes:

1. **Call/return**: when a colon-definition is entered (via
   `FF_OP_NEST`), the caller's `ip` is pushed; `FF_OP_EXIT` pops it to
   resume. A `NULL` return address signals the outermost level — the
   word was called from C, not from another Forth word — and causes
   `ff_exec` to return.

2. **Loop counters**: `FF_OP_XDO` pushes the loop limit, the loop index,
   and the after-loop address. `FF_OP_XLOOP` and `FF_OP_PXLOOP` update
   and test the index in place. `FF_OP_LEAVE` discards all three and
   jumps past the loop.

3. **Temporary storage**: the words `>r`, `r>`, and `r@` let Forth code
   stash and retrieve values across calls that would otherwise discard
   them.


## Backtrace stack

`ff_bt_stack_t` (defined in `ff_bt_stack_p.h`) is a fixed-size ring of
word pointers, separate from the return stack:

~~~{.c}
struct ff_bt_stack
{
    const ff_word_t *data[FF_BT_STACK_SIZE];   /* 256 frames */
    int top;
};

static inline void ff_bt_stack_push(ff_bt_stack_t *s, const ff_word_t *w)
{
    if (s->top < FF_BT_STACK_SIZE)
        s->data[s->top++] = w;
}
~~~

When `FF_STATE_BACKTRACE` is set, every word entry pushes onto this stack
and every exit (`do_exit`, `ff_w_exit`) pops. At any point the stack holds
the current call chain, accessible to the `backtrace` word.

`ff_exec` saves `bt_stack.top` on entry and restores it on exit regardless
of success or failure, so the backtrace stack never leaks frames across a
call boundary.

The push silently discards frames when the stack is full rather than
faulting — a deliberate choice to keep debug mode non-intrusive.


## Platform abstraction

~~~{.c}
typedef int (*ff_vprintf_fn)(void *ctx, const char *fmt, va_list args);
typedef int (*ff_vtracef_fn)(void *ctx, ff_error_t e, const char *fmt, va_list args);

typedef struct ff_platform
{
    void            *context;
    ff_vprintf_fn    vprintf;
    ff_vtracef_fn    vtracef;
} ff_platform_t;
~~~

All output from the engine is routed through these two callbacks. The
engine has no direct calls to `printf` or `fprintf`. This allows *ff* to be
embedded in environments that have no `stdout`, or to redirect output to
a log, a GUI widget, or a network socket with no changes to the library.

`vprintf` is used for all normal output (`.`, `." "`, `.s`).
`vtracef` is called for warnings, errors, and trace messages, and receives
the severity/error code so the caller can filter or format as needed.
If either callback is `NULL`, the corresponding output is silently
discarded.


## `create` and `does>`

`create` and `does>` are the standard Forth mechanism for defining new
defining words — words that create other words.

**`create name`** allocates a new dictionary entry whose `opcode` is
`FF_OP_CREATE_RUNTIME`. The dispatch case pushes the word's
`heap.data` pointer (its *parameter field*) onto the data stack.

**`does> …code… ;`** rewrites the most recently created word: its
`opcode` is changed to `FF_OP_DOES_RUNTIME`, and `ff->ip` (which now
points at the `does>` body inside the *defining* word) is captured
into the new word's `does` field. The defining word's compiled
sequence then ends with an early `EXIT` so the defining word's caller
sees a normal return.

At runtime, when a word produced by `does>` is invoked, the dispatch
arm is just a few inline instructions:

~~~{.c}
case FF_OP_DOES_RUNTIME:
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, ff->cur_word);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);   /* return frame */
        ff->cur_word = nw;
        ip = nw->does;                              /* jump to DOES> body */
        _PUSH_PTR(nw->heap.data);                   /* push pfa */
    }
    break;
~~~

Because everything happens inline inside the dispatch switch, there is
no out-of-line C call and no `ip` flush/reload — the instruction
pointer simply moves to the `does>` body and continues.


## `defer` and `is`

ANS Forth's late-binding facility — a word that can have its action
swapped out at runtime without redefining the name. Useful for
parameterising a long-lived definition over an action that the
embedder fills in later.

**`defer name`** creates a new dictionary entry with `opcode =
FF_OP_DEFER_RUNTIME` and reserves a single cell at `heap.data[0]` to
hold the target xt. The cell is initialised to zero, so executing
`name` before any action has been assigned raises `FF_ERR_BAD_PTR`
("Deferred word '<name>' has no action assigned.") and unwinds — no
NULL dispatch, no segfault.

**`' xt-source is name`** pops the xt left on the data stack by `'`,
parses `name` from the input stream, looks it up, verifies it's a
deferred word (`opcode == FF_OP_DEFER_RUNTIME`), and stores the xt
into `name->heap.data[0]`. The token-resolution side of `is` lives in
the `ff_eval` parser loop alongside the existing `'` and `forget`
look-ahead handlers; the dispatch-time half is just a setter that
flips `FF_STATE_IS_PENDING` so the next token gets routed to the
assignment path.

The runtime arm is a thin shim that dispatches through the slot:

~~~{.c}
case FF_OP_DEFER_RUNTIME:
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        ff_word_t *target = (ff_word_t *)(intptr_t)nw->heap.data[0];
        if (target == NULL)
            /* raise FF_ERR_BAD_PTR, goto done */;
        _FF_SYNC();
        ff_exec(ff, target);
        _FF_RESTORE();
    }
    break;
~~~

The slot is a plain cell, so `is` is just a store. There is no
peephole-folding, no compile-time specialisation: every call to a
deferred word is one indirection through `heap.data[0]` to the target
word, then a normal `ff_exec`. The cost is the same as one extra
`execute` per deferred call.

The richer SwiftForth/Brodie-flavoured pair (`doer`/`make`/`;and`,
where `make` compiles an inline action body inside the surrounding
definition) is *not* implemented; the data-structure shape would
support it on top of the same `FF_OP_DEFER_RUNTIME` slot if it
becomes useful.


## Error handling

Errors are reported through `ff_tracef(ff, severity | code, fmt, ...)`.
At `FF_SEV_ERROR` level the function writes the message to
`ff->error_msg`, stores `ff->error`, sets `FF_STATE_ERROR`, and returns
the error code.

`ff_exec` propagates failures by jumping to the `broken:` label which
sets `FF_STATE_BROKEN` and returns `false`. `ff_eval` detects the `false`
return and unwinds to its caller.

The `FF_STATE_BROKEN` flag survives through the stack until `ff_eval`
clears it on the next call. Its purpose is distinct from `FF_STATE_ERROR`:
broken means "stop executing the current chain", error means "there is a
diagnostic message to retrieve".

`ff_abort` resets all state flags, clears both stacks, and sets `ip` to
NULL, returning the interpreter to a clean idle state.


## Configuration

All buffer sizes are defined in `ff_config.h`:

| Constant | Default | Purpose |
|---|---|---|
| `FF_STACK_SIZE` | 512 | Data and return stack depth (cells) |
| `FF_BT_STACK_SIZE` | 256 | Backtrace stack depth (frames) |
| `FF_INIT_HEAP_SIZE` | 64 | Initial heap allocation for a new word (cells) |
| `FF_PAD_COUNT` | 128 | Temporary string ring slots |
| `FF_PAD_SIZE` | 256 | Bytes per pad slot |
| `FF_TOKEN_SIZE` | 256 | Tokenizer token buffer (bytes) |
| `FF_ERROR_MSG_SIZE` | 512 | Error message buffer (bytes) |
| `FF_LOAD_LINE_SIZE` | 4096 | Line buffer for `ff_load` (bytes) |

Define `FF_32BIT` at compile time to select 32-bit cell and single-
precision float modes for constrained targets. Define
`FF_SAFE_MEM=1` to enable the address-validation pass — see the next
section.


## Memory safety

The default build trusts addresses on the data stack: `@`, `!`, `+!`,
`c@`, `c!`, `s!`, `s+`, `strlen`, `strcmp`, `execute`, `evaluate`, and
`load` cast the relevant TOS cell to a pointer and dereference it
without validation. A bare `0 @` segfaults the host process — this
matches classical Forth semantics, where the language deliberately
exposes raw memory.

For embeddings that take untrusted Forth input — REPLs exposed over
the network, scripting hooks in long-lived servers, untrusted plugin
sources — that contract is wrong. *ff* offers an opt-in safe mode
controlled by a single compile-time flag.

### Enabling

Configure the build with `-DFF_SAFE_MEM=ON` (the CMake option) or
`-DFF_SAFE_MEM=1` (the C macro). Both forms gate the same checks; the
CMake option just propagates the macro through `add_compile_definitions`.

When the flag is off (the default), the check macros expand to
`((void)0)` and the compiler eliminates them entirely — zero text-
size and zero runtime cost.

### What gets checked

Each of the address-consuming primitives wraps its dereferences in
`FF_CHECK_ADDR(ff, addr, bytes)` or `FF_CHECK_XT(ff, w)`. Both return
`FF_ERR_BAD_PTR` via `ff_tracef` and unwind cleanly back to the
interpreter loop on a miss; the engine state remains consistent and
subsequent `ff_eval` calls work normally.

| Primitive | Check |
|---|---|
| `@`, `!`, `+!` | `FF_CHECK_ADDR(ff, addr, sizeof(ff_int_t))` |
| `c@`, `c!` | `FF_CHECK_ADDR(ff, addr, 1)` |
| `s!`, `s+` | `FF_CHECK_ADDR` on both source and destination, sized to the source string length |
| `strlen`, `strcmp` | `FF_CHECK_ADDR` on each argument (1 byte — `strlen` walks until NUL inside the verified region) |
| `execute` | `FF_CHECK_XT(ff, w)` — verifies the xt is a live `ff_word_t*` in the dictionary |
| `evaluate`, `load` | `FF_CHECK_ADDR(ff, addr, 1)` on the source / path string |

The validator predicate `ff_addr_valid(ff, addr, bytes)` accepts the
range only if `[addr, addr + bytes)` fits inside one of:

- the data stack (`ff->stack.data`)
- the return stack (`ff->r_stack.data`)
- the pad ring (`ff->pad`)
- any dictionary word's heap (`word->heap.data`, sized to capacity).

The dictionary is walked linearly per check — O(N\_words). For typical
embedded dictionaries (~150 builtins plus a handful of user words) the
per-call cost is negligible. Hot interpretive loops slow by roughly
10-20 % under the flag; tight `@`/`!`-heavy loops slow more.

### What is NOT covered

Safe mode is crash-resistance, not full memory safety. It defends
against accidental address corruption from buggy Forth code. It does
NOT defend against:

- **Crafted bytecode written via `,`.** A user can `compile`-time emit
  arbitrary opcode sequences into the current word's heap. If the
  sequence contains a `FF_OP_NEST` followed by a forged `word_ptr`,
  the inner interpreter dereferences that pointer without validation.
  Closing this hole would require either disabling `,` and `compile,`
  in safe mode, or tagging cells (the level-2 design).

- **Bugs in C code embedded inside *ff*.** A miswritten native word
  that segfaults takes the host with it. Sandbox the process if that
  matters.

- **Use-after-`forget`.** If a Forth program saves an xt or a buffer
  address into a variable, then `forget`s the word that owned it,
  reading from the variable still finds an address that was once
  valid. Currently the check sees the old address as no-longer-valid
  (the dict no longer contains the word, so its heap is no longer
  tracked) — so this is detected, not exploited. Confirmed by the
  validator's linear scan.

### Custom native words

When you write your own native words against `<ff_p.h>`, both the
`FF_CHECK_ADDR` and `FF_CHECK_XT` macros are visible. Use them at the
top of any word body that consumes a pointer or xt from the data
stack — see the *Extending* chapter for examples.
