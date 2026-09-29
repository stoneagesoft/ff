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
`ff_throw_p.h`, `ff_cf_p.h`, `ff_mem_p.h`, `ff_base_p.h`, `ff_token_p.h`,
`ff_tok_state_p.h`, `ff_word_flags_p.h`, `ff_word_def_p.h`.

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

    ff_word_t    *compiling;             /* definition being compiled */
    ff_cf_t       cf[FF_CF_DEPTH];       /* its open control structures */
    int           n_cf;
};
~~~

`compiling` and `cf` are the compiler's state between `:` and `;`; see
*Compiling a definition*.

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

`ff_state_t` (in `ff_state_p.h`) is a bitmask that governs the current
interpreter mode:

| Flag | Meaning |
|---|---|
| `FF_STATE_COMPILING` | Compile state: tokens are compiled into `ff->compiling`. `[` clears it while the definition stays open |
| `FF_STATE_SIG_PENDING` | Collecting a scope signature — the evaluator routes tokens to the signature parser, not the kind dispatch |
| `FF_STATE_TRACE` | Print each word name before executing it |
| `FF_STATE_BACKTRACE` | Maintain a call-chain for debugging |
| `FF_STATE_THROWN` | An exception is in flight (code in `ff->throw_code`); see *Error handling* |

No flag carries a request over to the next token: a word that takes a
name or a string reads it itself (see *Eval loop*). Only a `{`
signature, which may span lines, keeps a mode between tokens.


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

The tokenizer handles parenthesis comments that span lines (they don't
nest: the first `)` closes one), records each token's start offset in
`t->pos` so errors can be located, and understands the full set of
C-style backslash escape sequences plus Unicode escapes (`\uXXXX`,
`\UXXXXXXXX`, `\xXX`). A malformed numeric escape — too few hex digits, a
surrogate, a code point past U+10FFFF — sets `t->bad_escape`, which the
evaluator reports as `FF_ERR_MALFORMED`. Unicode encoding is delegated to
`utf8catcodepoint()` from the vendored `3rdparty/utf8/utf8.h` library.

Number parsing attempts integer first, then real. A `0x` literal is a
cell bit pattern (`0xFFFFFFFFFFFFFFFF` is -1) and is never retried as a
real; a decimal integer that overflows falls back to a real. A real is
an optional `-`, digits with a fraction and/or an exponent (`1.5`,
`1.`, `-.5`, `2e3`, `1.5e-7`), read by `ff_real_parse()` with `.` as the
decimal point whatever the host's C locale — `f.`, `.s` and `see` print
with `.` too (`ff_real_format()`). A value too small to be represented
normally reads as the nearest there is; one too large for a real, and
`inf` or `nan`, don't read as numbers. Words that cannot be parsed as
numbers are returned as `FF_TOKEN_WORD`.


## Eval loop

`ff_eval(ff, src)` is the outer interpreter. It tokenizes the input string
in a loop and dispatches on the current state:

**Interpret mode** (default): each word is looked up and executed
immediately via `ff_exec`; integers and reals are pushed to the data stack.

**Compile mode** (`FF_STATE_COMPILING` set by `:`): words are compiled
into the heap of the definition being compiled, `ff->compiling`, rather
than executed, unless the word carries `FF_WORD_IMMEDIATE`, in which case
it executes at compile time. Integers are compiled as one of the specialised
literals (`FF_OP_LIT0`/`LIT1`/`LITM1`) when possible, otherwise as
`FF_OP_LIT` + value; reals always go through `FF_OP_FLIT` + bit-cast
value.

The `[` word temporarily clears `FF_STATE_COMPILING`, allowing interpret-
mode evaluation inside a definition. `]` restores compile mode.
`['word']` compiles a word's address as a literal. What happens to a
definition across lines and errors is described under *Compiling a
definition*.

**Parsing words** — `:`, `create`, `variable`, `'`, `[']`, `postpone`,
`forget`, `is`, `see`, `."`, `.(`, `abort"` and the others that take a
name or a string — read it from the input themselves when they run,
through `ff_parse()`, as standard Forth words parse the input stream.
Before running a word the evaluator leaves its place in `ff->input` /
`ff->input_pos`, and afterwards carries on from `ff->input_pos`, past
whatever the word read. A word run from compiled code reads the same
input, so `: mk create ;  mk name` names the word `mk` makes, and a
word that runs `create` twice makes two. The name or string must follow
in the same input — `ff_parse()` raises `FF_ERR_MISSING` at its end, as
it does for a token of the wrong kind. `ffsh` and `ff_load()` pass one
line per `ff_eval()` call, so there it means the same line; a string
passed to `ff_eval()` or `evaluate` is one input however many lines it
spans. A word run by the host's `ff_exec()` outside any evaluation has
no input to read.


## Word structure

~~~{.c}
struct ff_word
{
    char             *name;        /* null-terminated; strdup'd or aliased literal */
    ff_opcode_t       opcode;      /* assigned opcode, or FF_OP_NONE */
    ff_word_flags_t   flags;       /* IMMEDIATE, USED, HIDDEN, STATIC, NATIVE */
    ff_word_fn        fn;          /* external native's C function, else NULL */
    ff_int_t          stub[3];     /* executable form: call sequence, then EXIT */
    ff_int_t         *does;        /* DOES> clause start (NULL if none) */
    ff_heap_t         heap;        /* compiled body, or the data of create & co. */
    ff_sig_t         *sigs;        /* scope signatures by bytecode offset (see); NULL if none */
    size_t            sigs_len;    /* count of sigs */
    const char       *manual;      /* help text string (may be NULL) */
    const char       *man_desc;    /* pointer past first '\n' in manual */
    struct ff_word   *next_bucket; /* dict hash chain (newest first) */
};
~~~

Every word is dispatched through its `opcode`:

- **Built-in words** carry a real opcode (`FF_OP_DUP`, `FF_OP_ADD`,
  `FF_OP_NEST`, `FF_OP_DOES_RUNTIME`, …) and the case body lives in
  `words/ff_words_*_p.h`. Compiling such a word emits one cell for the
  opcode plus zero or one inline operand.

- **External native words** added by an embedder use the `FF_OP_CALL`
  escape hatch. They carry `opcode = FF_OP_NONE`, the `FF_WORD_NATIVE`
  flag, and their `void (*)(ff_t *)` function in `fn`; their heap stays
  empty. A compiled caller emits the two-cell sequence `FF_OP_CALL`,
  `fn`.

- **Colon-definitions** carry `FF_OP_NEST` (or `FF_OP_TNEST` after
  tail-call peephole optimisation) and a real bytecode body in `heap`.

- **`create`/`does>`/`constant`/`array` words** carry one of
  `FF_OP_CREATE_RUNTIME`, `FF_OP_DOES_RUNTIME`,
  `FF_OP_CONSTANT_RUNTIME`, `FF_OP_ARRAY_RUNTIME`, all of which read
  the word's `heap.data` (and `does` for `FF_OP_DOES_RUNTIME`).

`flags` carries OR-able bits including `FF_WORD_IMMEDIATE` (compile-
time word), `FF_WORD_USED` (set on lookup; reported by `wordsunused`),
`FF_WORD_HIDDEN` (omit from `words` listing), `FF_WORD_NATIVE` (call
through `fn`), and `FF_WORD_STATIC` (struct + name belong to
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

- **In-place growth doesn't move addresses.** A word's heap can grow
  as the body extends (`,`, `allot`, post-`;` `does>`-clause
  attachment) without disturbing the addresses baked into any other
  word's compiled bytecode. A contiguous heap could not grow without
  invalidating every cross-reference baked during earlier compilations.
  A heap that is the arena's newest allocation — the definition being
  compiled, a data word being filled — grows where it lies; one that
  isn't moves to a fresh region, and the copy it leaves behind is
  reclaimed only when words are removed.

- **`forget` releases memory cleanly.** Word heaps are carved from a
  slab arena, and each heap records where its region in the arena ends.
  When `forget name` cascades through everything defined after `name`,
  the arena rolls back to the end of the last heap still in use — slabs
  made since are freed, and the one it ends in is rewound — so a
  define/forget cycle holds memory steady. Forget cascades for the same reason it
  does in standard Forth (later words may have baked in addresses
  pointing at earlier words' bytecode entry points). A definition that
  fails to compile is removed the same way (see *Compiling a
  definition*).

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
- **External native (`FF_OP_NONE`, `FF_WORD_NATIVE`)**: two cells —
  `FF_OP_CALL`, `(ff_int_t)(intptr_t)ff_word_native_fn(w)`.
- **The definition being compiled (`FF_OP_NONE`, not native)** — a word
  calling itself: two cells, `FF_OP_NEST`, `w`. It gets its own
  `FF_OP_NEST` opcode only at `;`.

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

**Example — `: greet ." "Hello" cr ;`**

~~~
heap.data:
  [0]  FF_OP_PRINT_STR
  [1]  2              ← skip count (cells to advance past string)
  [2]  "Hello\0"      ← string bytes packed into one 8-byte cell
  [3]  FF_OP_CR
  [4]  FF_OP_EXIT
~~~

(`cr` is a built-in opcode so the call collapses to one cell. An
embedder-supplied native would be the two-cell `FF_OP_CALL`, `fn_ptr`
sequence instead.)


## Compiling a definition

`:` creates the word and makes it `ff->compiling`, the one place compiled
code goes until `;`. It is not simply the newest word in the dictionary:
a `create` or `variable` run between `[` and `]` adds a newer one, and
the rest of the definition must not follow it there. The word is visible
under its name from the start, so a definition can call itself by name;
`recurse` compiles the same call. Definitions don't nest — `:` while one
is open raises -29 — and `forget` is refused while one is open, since it
would free the word under the compiler.

**Abandoning a definition.** A definition that doesn't reach `;` must not
stay behind: its body has no `EXIT` yet, and its opcode is still
`FF_OP_NONE`. So an exception that discards the input a definition is
being compiled from also removes the word (`ff_dict_remove`), along with
the compiler's records for it. Which input that is follows the
evaluation depth: `:` records `ff->eval_depth` in `ff->def_depth`, and an
exception that ends an `ff_eval` at that depth or a shallower one
abandons the definition. One caught deeper leaves it open, so
`: w [ "zork" evaluate ] literal ;` compiles the -13 that `evaluate`
pushes. An uncaught `abort` and `ff_abort()` abandon it too.

**Control structures** are kept on a stack of their own, `ff->cf`, not
on the data stack as ANS permits. Each record (`ff_cf_p.h`) holds its
kind (the ANS *orig*, *dest* or *do-sys*), the word that opened it for
error messages, the heap index it refers to, and the number of `{`
scopes open when it was opened. Every closer checks the top record: that
there is one, that it is of the kind the closer takes, and that it was
opened in the current scope — a branch across a scope boundary would
skip its `SCOPE_ENTER` or `SCOPE_EXIT`. `}` and `;` check that nothing
opened inside them is still open. A failed check raises -22. `while`
puts its *orig* under the *dest*, as in ANS, so a loop may have several
`while`s, each after the first closed by a `then` after the `repeat`.

Keeping the records off the data stack means a number there can never be
mistaken for a branch to patch, and the data stack stays the program's
while it compiles: `[ 2 3 + ] literal` inside an `if` works.

**Leaving early.** `exit` and `leave` jump out of whatever encloses them,
so `ff_compile_call`, the one path that compiles a call, first closes
what they leave: each `{` scope with `FF_OP_SCOPE_UNWIND` (the same
operation as `}`'s `SCOPE_EXIT`, outputs checked, under an opcode of its
own so that `see` can leave it out) and, for `exit`, each counted loop
with `FF_OP_UNLOOP`, innermost first. `leave` needs a `do` in the same
definition. `does>` also ends the running word, so it may not stand
inside a loop or a scope.

**Parsing words** only parse. `."` and `abort"` in a definition compile
their string after a run-time primitive, `FF_OP_PRINT_STR` or
`FF_OP_ABORTQ_RUNTIME`; `.(` prints its string at once, in a definition
too; `abort"` at the prompt throws at once. `compile w` compiles
`FF_OP_POSTPONE_RUNTIME w`, which compiles a call to `w` when the
definition runs.

**`forget`** is refused while a definition is open, and while any word
it would remove is running — `ff->cur_word`, or a caller saved in a
return frame, including a word that called `evaluate` or `catch`: its
code would be freed under it. Words defined after the running ones can
still be forgotten, from inside `evaluate` as anywhere else.


## Opcode set

Every built-in word — including the structural ones, the `does>` /
`create` / `constant` / `array` runtimes, and every immediate compiler
word — has a dedicated opcode. The full set has grown to over 200
entries, grouped as follows. The canonical list is `FF_OPCODES` in
[ff_opcode_p.h](src/ff_opcode_p.h): one `X(name, layout)` line per
opcode, from which both the `ff_opcode_t` enum and the operand-layout
table (`ff_opcode_layout`, used by `see`, `dump-word`, the stubs and the
compiler) are generated. A built-in word's name, immediacy and manual
live in its registration table (`words/ff_words_*.c`), and `see` names an
opcode after the word registered for it.

| Group | Examples |
|---|---|
| Structural | `FF_OP_CALL`, `FF_OP_NEST`, `FF_OP_TNEST`, `FF_OP_EXIT`, `FF_OP_BRANCH`, `FF_OP_QBRANCH` |
| Literals | `FF_OP_LIT`, `FF_OP_LIT0`, `FF_OP_LIT1`, `FF_OP_LITM1`, `FF_OP_LITADD`, `FF_OP_LITSUB`, `FF_OP_FLIT`, `FF_OP_STRLIT`, and the string runtimes `FF_OP_PRINT_STR` (`."`) and `FF_OP_ABORTQ_RUNTIME` (`abort"`) |
| Defining-word runtimes | `FF_OP_CREATE_RUNTIME`, `FF_OP_DOES_RUNTIME`, `FF_OP_CONSTANT_RUNTIME`, `FF_OP_ARRAY_RUNTIME`, `FF_OP_DEFER_RUNTIME`, `FF_OP_VAR_FETCH`/`VAR_STORE`/`VAR_PLUS_STORE` (peephole) |
| Stack manipulation | `FF_OP_DUP`, `FF_OP_DROP`, `FF_OP_SWAP`, `FF_OP_OVER`, `FF_OP_NIP`, `FF_OP_TUCK`, `FF_OP_ROT`, `FF_OP_NROT`, `FF_OP_PICK`, `FF_OP_ROLL`, `FF_OP_DEPTH`, `FF_OP_CLEAR`, `FF_OP_TO_R`, `FF_OP_FROM_R`, `FF_OP_FETCH_R` |
| Two-cell stack ops | `FF_OP_2DUP`, `FF_OP_2DROP`, `FF_OP_2SWAP`, `FF_OP_2OVER` |
| Integer math / bitwise / compare | `FF_OP_ADD`/`SUB`/`MUL`/`DIV`/`MOD`/`DIVMOD`, `FF_OP_MIN`/`MAX`/`NEGATE`/`ABS`, `FF_OP_AND`/`OR`/`XOR`/`NOT`/`SHIFT`, `FF_OP_EQ`/`NEQ`/`LT`/`GT`/`LE`/`GE`, `FF_OP_ZERO_EQ`/`ZERO_NEQ`/`ZERO_LT`/`ZERO_GT`, `FF_OP_INC`/`DEC`/`INC2`/`DEC2`/`MUL2`/`DIV2`, `FF_OP_SET_BASE` |
| Floating-point | `FF_OP_FADD`/`FSUB`/`FMUL`/`FDIV`, `FF_OP_FNEGATE`/`FABS`/`FSQRT`, `FF_OP_FSIN`/`FCOS`/`FTAN`/`FASIN`/`FACOS`/`FATAN`/`FATAN2`, `FF_OP_FEXP`/`FLOG`/`FPOW`, `FF_OP_F_DOT`/`FLOAT`/`FIX`/`PI`/`E_CONST`, `FF_OP_FEQ`/`FNEQ`/`FLT`/`FGT`/`FLE`/`FGE` |
| Console I/O | `FF_OP_DOT`, `FF_OP_QUESTION`, `FF_OP_CR`, `FF_OP_EMIT`, `FF_OP_TYPE`, `FF_OP_DOT_S`, `FF_OP_DOT_PAREN`, `FF_OP_DOTQUOTE` |
| Counted loops | `FF_OP_XDO`, `FF_OP_XQDO`, `FF_OP_XLOOP`, `FF_OP_PXLOOP`, `FF_OP_LOOP_I`, `FF_OP_LOOP_J`, `FF_OP_LEAVE`, `FF_OP_UNLOOP` (before an `exit` from a loop), `FF_OP_I_ADD` (peephole `i +`), `FF_OP_I_ADD_LOOP` (peephole `i + loop`) |
| Compiler / immediate | `FF_OP_COLON`, `FF_OP_SEMICOLON`, `FF_OP_IMMEDIATE`, `FF_OP_LBRACKET`, `FF_OP_RBRACKET`, `FF_OP_TICK`, `FF_OP_BRACKET_TICK`, `FF_OP_EXECUTE`, `FF_OP_STATE`, `FF_OP_BRACKET_COMPILE`, `FF_OP_LITERAL`, `FF_OP_COMPILE`, `FF_OP_POSTPONE`, `FF_OP_POSTPONE_RUNTIME`, `FF_OP_RECURSE`, `FF_OP_DOES` |
| Control flow | `FF_OP_QDUP`, `FF_OP_IF`/`ELSE`/`THEN`, `FF_OP_BEGIN`/`UNTIL`/`AGAIN`, `FF_OP_WHILE`/`REPEAT`, `FF_OP_DO`/`QDO`/`LOOP`/`PLOOP`, `FF_OP_QUIT`, `FF_OP_ABORT`, `FF_OP_ABORTQ`, `FF_OP_THROW`, `FF_OP_CATCH` |
| Stack scopes | `FF_OP_LBRACE`/`RBRACE` (immediate `{`/`}`), `FF_OP_SCOPE_ENTER`, `FF_OP_SCOPE_EXIT`, `FF_OP_SCOPE_UNWIND` (before an `exit` or `leave` out of a scope), `FF_OP_ARG` |
| Definitions | `FF_OP_CREATE`, `FF_OP_FORGET`, `FF_OP_VARIABLE`, `FF_OP_CONSTANT`, `FF_OP_ARRAY`, `FF_OP_DEFER`, `FF_OP_IS` |
| Heap | `FF_OP_HERE`, `FF_OP_STORE`/`FETCH`/`PLUS_STORE`, `FF_OP_ALLOT`/`COMMA`, `FF_OP_C_STORE`/`C_FETCH`/`C_COMMA`/`C_ALIGN` |
| Strings | `FF_OP_STRING`, `FF_OP_S_STORE`, `FF_OP_S_CAT`, `FF_OP_STRLEN`, `FF_OP_STRCMP` |
| Evaluation / parsing | `FF_OP_EVALUATE`, `FF_OP_LOAD`, `FF_OP_PARSE_WORD`, `FF_OP_PARSE` |
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

Every word carries a tiny executable form of itself, its *stub*
(`ff_word_t::stub`): the call sequence the compiler would emit for it,
followed by `FF_OP_EXIT` — `[NEST w EXIT]` for a colon definition,
`[DUP EXIT EXIT]` for a primitive, `[CALL fn EXIT]` for an external
native. The stub is built when the word is created and rebuilt whenever
its opcode changes (`ff_word_set_opcode`). To start a word, `ff_exec`
pushes a return frame whose saved `ip` is `NULL` and points `ip` at the
stub; the stub's final `EXIT` pops that frame and ends the run:

~~~{.c}
bool ff_exec(ff_t *ff, ff_word_t *w)
{
    ff_stack_t    *S  = &ff->stack;
    ff_stack_t    *R  = &ff->r_stack;
    ff_bt_stack_t *BT = &ff->bt_stack;

    ff->cur_word = w;
    int bt_size = BT->top;

    /* …trace / backtrace gating, overflow check elided… */

    ff_stack_push(R, 0);                /* NULL return sentinel */
    ff_stack_push(R, (ff_int_t)(intptr_t)prev_cur_word);
    ff_int_t *ip = w->stub;

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
            _FF_CHECK_THROWN();         /* the native raised an error */
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

`execute` and deferred words use the same stubs without leaving the
loop: they push a return frame exactly as `NEST` does and jump to the
target's stub. Neither re-enters `ff_exec` in C, so their call depth is
bounded by the return stack like any other call, and an exception inside
the target unwinds through the caller like any other. The only C
re-entry left is where a word evaluates source (`evaluate`, `load`), runs
an exception frame (`catch`), or calls a native word — each of which
checks for an exception when the nested run returns.

An operand-free opcode can tell whether it is running straight from a
stub — the interpreter or `execute` running the word itself — rather
than from compiled code: `ip` then sits just past the stub's first cell
(`_FF_RUNNING_DIRECT`). `'`, `.(` and `does>` use that to choose between
their interactive and compiled behaviour.


## Stack scopes

A *scope* is an opt-in `{ ( a b -- c ) … }` construct that names a
definition's inputs and walls off the data stack for the enclosed code.
It exists to make the stack comment every Forth programmer already
writes into something the engine checks and the decompiler can show,
without giving up the RPN model underneath — a scoped word compiles to
the same token-threaded bytecode as any other and unscoped code is
untouched.

### The barrier

The data stack (`ff_stack_t`) carries a `floor` alongside `top`. The
underflow check that every stack-consuming word already runs measures
available depth as `top - floor` rather than `top - 0`:

~~~{.c}
#define _FF_SL(n) \
    do { if (ff_unlikely((int)(S->top - floor) < (int)(n))) …underflow… } while (0)
~~~

`floor` starts at 0, so outside any scope the check is unchanged.
`FF_OP_SCOPE_ENTER` raises it to the current `top` (saving the old value
on a per-engine record stack, `ff->scopes`); `FF_OP_SCOPE_EXIT` restores
it. While a scope is open, the enclosed code sees an empty stack: it may
push and pop freely, but a word that reaches below what it pushed hits
the floor and faults with `FF_ERR_STACK_UNDER` — the caller's cells are
unreachable, so a stray `drop` can no longer silently corrupt them.

`floor` is register-cached in `ff_exec` next to `tos` and `ip`, since
`_FF_SL` reads it on every stack word; `SCOPE_ENTER`/`EXIT` write both
the register and the memory copy so they stay coherent across the
`_FF_SYNC`/`_FF_RESTORE` that bracket every native call. The barrier
also governs external native words: they validate through the public
`FF_SL` (in `ff_p.h`), which checks the same `floor`, so a native word
invoked inside a scope cannot reach past it either.

Because the record stack is indexed by *call* depth (a recursive scoped
word holds one record per active invocation), it is sized like the
back-trace stack, `FF_SCOPE_DEPTH = 256`. `catch` snapshots `floor` and
the record count and rolls them back if a `throw` unwinds out of an open
scope, so the barrier never leaks.

### Named inputs

The signature `( a b -- c )` is parsed by the evaluator, not a case
body — the tokenizer sits a level above the dispatch switch. `{` sets
`FF_STATE_SIG_PENDING` and `FF_TOK_STATE_SIG` (the latter suspends
`( … )` comment handling so the signature arrives as ordinary tokens),
and the evaluator routes the following tokens to a small state machine
until the closing `)`. Names left of `--` are collected; the count right
of `--` is the declared output arity; `--` and `)` delimit the regions.

Inside the body a name resolves *before* the dictionary and compiles to
`FF_OP_ARG k`, an indexed read of `data[floor - k]` (rightmost input =
`k` 1, next `k` 2, and so on). No dictionary entry is ever created, so a name cannot collide,
needs no cleanup at `}`, and shadows any word of the same spelling for
the body of its scope — a purely lexical binding. Only the innermost
scope's names are visible: an enclosing name would address the enclosing
floor, which the inner scope cannot reach, so nesting is a compile-time
stack of name→index maps with no cross-talk.

At `}`, `FF_OP_SCOPE_EXIT` checks that the scope produced exactly its
declared number of cells (`FF_ERR_SCOPE_ARITY` otherwise), asserts the
return stack is back to its entry depth (`FF_ERR_RSTACK_IMBAL`), then
slides the produced cells down over the consumed inputs with a single
`memmove` and restores the floor.

### Operand packing and the peephole

`SCOPE_ENTER` and `SCOPE_EXIT` each carry exactly one inline operand
cell, with their fields bit-packed (`FF_SCOPE_PACK_ENTER` /
`FF_SCOPE_PACK_EXIT`). This is not incidental: the tail-call peephole in
`;` reads `data[size - 2]` expecting an opcode, and a two-cell operand
encoding would leave a small integer there that aliases `FF_OP_NEST`
(== 1) for a one-input scope. The peephole was made operand-aware to
match — it now consults `ff_opcode_layout` (via `ff_heap_op_starts_at`) to
confirm `data[size - 2]` actually begins an instruction before rewriting
it, rather than assuming a fixed stride.

A trailing scope therefore cannot fold to `FF_OP_TNEST`: `SCOPE_EXIT`
must run after the final call to slide the outputs, and "somewhere to
return to after the call" is exactly what a tail call gives up. This is
inherent, not a defect; scoped tail recursion is left to a possible
future `SCOPE_TNEST` and is not implemented.

### Signatures and `see`

The signature *source text* is stored on the owning word in a side table
(`ff_sig`), keyed by the bytecode offset of its `SCOPE_ENTER` — kept off
the execution path entirely, so the run time pays nothing for it. It is
keyed by offset because one word may open several nested scopes, and
stored as text (not a parsed array) so `...` and any future notation
round-trip without a schema change. `see` walks the body, opens a scope
frame when it meets `SCOPE_ENTER`, prints the stored signature, and
resolves each `FF_OP_ARG` back to the name it was written as by
re-splitting that text — including for arguments used inside an
`if`/`do`/`begin`, where the decompiler searches down its frame stack
past the control-flow frames to the enclosing scope.

### `...` escape hatches

Two forms trade a check for flexibility. `( a -- ... )` names its inputs
and installs a fresh barrier but skips the output-arity check at `}`
(`var_out`). `( ... -- ... )` takes no names and *inherits* the
enclosing floor rather than installing a new one (`var_in`), so a
variadic helper can still reach the caller's cells — but only as far as
the scope that contains it, one level of degradation rather than a full
opt-out. `...` on the input side is rejected alongside named inputs
(names would dangle once the code is permitted to pop below the floor)
and forces `...` on the output side (with the inputs consumed from
below, there is no reference point left to verify an output count
against — a checked output there could only ever be a comment, which is
the thing scopes exist to eliminate).

Scopes are compile-mode only: `{` uses the same `_FF_COMPILING` guard as
`if`/`do`/`begin` and reports cleanly at the top-level prompt.


## Input-stream words

Three words let Forth code reach the source text directly, which is what
makes new notation definable from inside the language rather than only in
C:

- **`parse-word ( -- c-addr )`** returns the next whitespace-delimited
  token as a NUL-terminated string in the pad arena (`ff_pad_intern`).
  Unlike `'`, it does *not* look the token up — it hands back raw text,
  and at the end of the input an empty string rather than an error. It
  reads `ff->input` / `ff->input_pos` as the other parsing words do (see
  *Eval loop*).
- **`parse ( char -- c-addr )`** scans `ff->input` from the current
  position to the next occurrence of the delimiter character, consuming
  it, and returns the text before it. It bypasses the tokenizer and does
  not skip leading delimiters — standard `parse` semantics.
- **`postpone`** appends a word's *compilation* semantics to the current
  definition. An immediate word, it parses the next token, looks it up
  and, knowing its immediacy, either compiles a direct call (immediate
  target — it should run when the new definition runs) or emits
  `FF_OP_POSTPONE_RUNTIME` carrying the word pointer (non-immediate
  target — a call to it should be *compiled* when the new definition
  runs). The runtime opcode calls `ff_heap_compile_word` on the
  definition then in progress, so it handles multi-cell colon-defs
  correctly, which the older single-cell `compile` primitive does not.

Because ff already exposes `evaluate`, `parse-word` closes the loop: a
parsing word can read tokens, assemble a string, and `evaluate` it —
enough to build conditional compilation, enum blocks, or other DSL
syntax without touching the engine. The regression suite's
`015_parse.ff` defines `[if]` / `[then]` this way.


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
ff_int_t *ip = w->stub;   /* …or wherever the word's stub leads… */

#define _FF_SYNC()    do { ff->ip = ip; _SYNC_TOS(); } while (0)
#define _FF_RESTORE() do { ip = ff->ip; _LOAD_TOS(); } while (0)
~~~

The compiler can allocate `ip` to a hardware register. Each opcode's
`*ip++` then advances a register, not a memory location. `_FF_SYNC()`
flushes the register back to the struct before any opcode body that
calls out to arbitrary C — primarily `FF_OP_CALL` (external natives)
and the few opcodes that re-enter `ff_eval` or `ff_load` — and
`_FF_RESTORE()` reloads on return. For that to work, every `ff_exec`
hands `ff->ip` back as it found it: whatever the C code ran in between
— `evaluate`, `catch`, or a native word's own `ff_eval()` or
`ff_exec()` call — the caller resumes where it synced.


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


### Two-op peephole superinstructions

The compiler tracks the last opcode it emitted in `heap.last_op` and
folds the following common two-op patterns into single dispatches:

| Source pair | Folded opcode | Notes |
|---|---|---|
| `0 +` / `0 -` | (eliminated) | identity, drops the `LIT0` |
| `0 =` | `FF_OP_ZERO_EQ` | branchless flag |
| `0 <>` | `FF_OP_ZERO_NEQ` | |
| `0 <` / `0 >` | `FF_OP_ZERO_LT` / `ZERO_GT` | |
| `1 +` / `1 -` | `FF_OP_INC` / `DEC` | |
| `2 +` / `2 -` / `2 *` / `2 /` | `FF_OP_INC2` / `DEC2` / `MUL2` / `DIV2` | |
| `LIT n  +` / `LIT n  -` | `FF_OP_LITADD` / `LITSUB` | one-cell save |
| `i +` | `FF_OP_I_ADD` | adds loop index in place |
| `i + loop` | `FF_OP_I_ADD_LOOP` | fused index-add and back-branch |
| `swap drop` | `FF_OP_NIP` | also a standalone primitive |
| `swap over` | `FF_OP_TUCK` | also a standalone primitive |
| `over +` | `FF_OP_OVER_PLUS` | base+offset idiom |
| `r@ +` | `FF_OP_R_PLUS` | index-relative offset |
| `<var> @` | `FF_OP_VAR_FETCH` | one-cell read direct from heap.data[0] |
| `<var> !` | `FF_OP_VAR_STORE` | one-cell store direct to heap.data[0] |
| `<var> +!` | `FF_OP_VAR_PLUS_STORE` | one-cell add-store |

The `<var> @`/`!`/`+!` group is particularly load-bearing: a naive
`v @` was three dispatches (`CREATE_RUNTIME` → push address → `FETCH`
→ dereference); the fused `VAR_FETCH` is one dispatch and one read.
The `[var-rmw](doc/md/50-benchmarks.md)` benchmark dropped from 680 ms
to 250 ms (2.7× speed-up, beating gforth-itc) on this fix alone.

#### Inhibiting peephole across branches

Forth's compile-time control words (`THEN`, `BEGIN`, `ELSE`, `REPEAT`,
`LOOP`, `+LOOP`) all create or patch branch targets. Folding across
those targets would silently mis-execute conditional code, because
the merged opcode would be reachable from a branch that previously
landed mid-pair. After every such word the engine calls
`ff_heap_inhibit_peephole(h)`, which clears `last_op` and breaks
the chain. Custom immediate words that emit branch targets must do
the same.


### Exceptions stay off the hot path

An error, `throw`, `abort` or `quit` stops execution at the point where
it is raised — the case body jumps straight to `ff_exec`'s exit (see
*Error handling*). Nothing in the dispatch loop has to poll for it:
`EXIT`, branches and loops carry no error check at all. The only checks
sit after the few calls out to C that can raise (`FF_OP_CALL`, `catch`,
`evaluate`, `load`, the introspection helpers), which are off the hot
path anyway.


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
    /* Ordered array (newest at end). Used by ff_dict_top, ff_dict_truncate,
       and introspection (`words`, `see`, …). */
    ff_word_t **words;
    size_t      count;
    size_t      capacity;        /* grows by doubling */

    /* The same words sorted by address: `execute`'s xt check under
       FF_SAFE_MEM is a binary search. */
    ff_word_t **by_addr;

    /* Hash buckets: each bucket is a singly-linked list (newest first)
       threaded through ff_word::next_bucket. */
    ff_word_t **buckets;
    size_t      bucket_count;    /* power of two — masking replaces modulo;
                                    doubles once it holds more words */

    /* Static pool: one big calloc holding all built-in word structs.
       Each entry carries FF_WORD_STATIC so ff_word_free skips it; the
       block is freed wholesale by ff_dict_destroy. Replaces what used
       to be ~150 separate mallocs at startup. */
    ff_word_t  *static_pool;
    size_t      static_pool_size;
};
~~~

**Lookup** hashes the name into a bucket and walks the bucket list
newest-first, so a newly defined word shadows an older one with the same
name. Names are hashed and compared with ASCII letters folded to lower
case and every other byte as it is, so `Dup` finds `dup` but `É` is not
`é`. On a hit the word's `FF_WORD_USED` flag is set
— `wordsunused` consults it to report dead definitions.

**Append** (`ff_dict_append`) puts the word at the end of `words`, in
place in `by_addr`, and at the head of its hash bucket; the table
doubles once it holds more words than buckets, so chains stay short.

**Forget** removes the named word and every word defined after it,
truncating `words` and rebuilding `buckets` from scratch. It then hands
the arena back past the end of the last heap still in use — a
definition that went on growing after a word was created between its
`[` and `]` can end beyond words removed with it.

**Remove** (`ff_dict_remove`) takes out one word and leaves the words
after it in place. The compiler uses it to drop a definition that failed:
nothing older can refer to it.

The static pool is filled once per process by `ff_builtins_init`,
which walks the per-category `FF_*_WORDS` registration tables and
stamps the corresponding pool slots. Built-in word names are taken by
reference from the def-table string literals (no `strdup`).


## Return stack

The return stack (`ff->r_stack`) is a second `ff_stack_t` that serves
three purposes:

1. **Call/return**: entering a word — `FF_OP_NEST` for a colon
   definition, and `execute`, deferred words and `does>` words alike —
   pushes a two-cell frame: the caller's `ip`, then the caller's word
   (`ff->cur_word`, on top). `FF_OP_EXIT` pops both to resume. A `NULL`
   return address signals the outermost level — the word was called
   from C, not from another Forth word — and causes `ff_exec` to return.

2. **Loop counters**: `FF_OP_XDO` pushes the after-loop address, the
   loop limit and the loop index (on top). `FF_OP_XLOOP` and
   `FF_OP_PXLOOP` update and test the index in place. `FF_OP_LEAVE`
   discards all three and jumps to the after-loop address.

3. **Temporary storage**: the words `>r`, `r>`, and `r@` let Forth code
   stash and retrieve values across calls that would otherwise discard
   them.

A safe build records which of these each cell is, so that a program's
`>r` can't pose as a frame or a loop (*Memory safety*, *The return
stack*).


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
sees a normal return. Because of that exit, `does>` may not be compiled
inside a `do` loop or a `{ }` scope, whose run-time state it would leave
behind. And the word it rewrites must be one `create` made: a colon
definition goes on being entered by the code that already calls it, so
its bytecode can't become data.

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
into `name->heap.data[0]`. `is` is immediate and state-smart, as in ANS
Forth: in a definition it parses and checks `name` while compiling, and
compiles `FF_OP_IS_RUNTIME name`, which pops the xt and stores it when
the definition runs — `: use-ten ['] ten is hook ;`.

Removing words — `forget`, or a definition that fails — resets a
deferred word left behind whose action was one of them to no action at
all, so running it is the error above rather than a call into freed
memory. The action is looked for among the words, never read through.
An xt a program keeps elsewhere, in a variable say, is its own to let
go of.

The runtime arm is a thin shim that dispatches through the slot:

~~~{.c}
case FF_OP_DEFER_RUNTIME:
    {
        ff_word_t *nw = (ff_word_t *)(intptr_t)*ip++;
        ff_word_t *target = (ff_word_t *)(intptr_t)nw->heap.data[0];
        if (target == NULL)
            /* raise FF_ERR_BAD_PTR, goto done */;
        _FF_RSO(2);
        ff_stack_push(R, (ff_int_t)(intptr_t)ip);      /* return frame */
        ff_stack_push(R, (ff_int_t)(intptr_t)ff->cur_word);
        ff->cur_word = target;
        ip = target->stub;                             /* run the target */
    }
    break;
~~~

The slot is a plain cell, so `is` is just a store. There is no
peephole-folding, no compile-time specialisation: every call to a
deferred word is one indirection through `heap.data[0]` to the target
word's stub, entered under a return frame inside the dispatch loop. The
cost is the same as one extra `execute` per deferred call.

The richer SwiftForth/Brodie-flavoured pair (`doer`/`make`/`;and`,
where `make` compiles an inline action body inside the surrounding
definition) is *not* implemented; the data-structure shape would
support it on top of the same `FF_OP_DEFER_RUNTIME` slot if it
becomes useful.


## Error handling

Every error is an exception, and so are `throw`, `abort`, `abort"`,
`quit` and a watchdog abort. An exception carries a THROW code in ANS
numbering (`ff_throw_p.h`): an engine error raised through
`ff_tracef(ff, FF_SEV_ERROR | code, fmt, ...)` gets the standard code for
its `FF_ERR_*` where one exists (-4 stack underflow, -10 division by zero,
-13 undefined word, …) and `-(256 + code)` otherwise; a `throw` from Forth
code carries whatever value the program gave it.

Raising records the code in `ff->throw_code` (plus, for an error, the
message and source location) and sets `FF_STATE_THROWN`. The raising case
body then jumps to `ff_exec`'s exit, which cuts the return stack and the
scope barrier back to where that invocation started and returns `false`.
Every place that re-entered the interpreter from C checks the flag when
the nested run returns and keeps unwinding unless it can settle the
exception:

| Where | What happens to the exception |
|---|---|
| `catch` | Restores the depths it saved, pushes the code, clears the flag. |
| `evaluate`, `load` | The nested `ff_eval` / `ff_load` stops; the code is pushed (0 on success). |
| `FF_OP_CALL` (native word) | Keeps unwinding: the word that called the native stops too. |
| `ff_eval`, `ff_load` | The evaluation ends; the call returns the `FF_ERR_*` code. |
| `ff_exec` called by the host | Returns `false`, leaving the engine clean for the next call. |

Two exceptions can't be caught: the watchdog / `ff_request_abort()`
(-28), which untrusted code must not be able to swallow, and `quit`
(-56), which exists to return to the host. They pass through `catch`,
`evaluate` and `load` and stop only at the outermost API call — which
returns `FF_ERR_ABORTED` for the former and `FF_OK` for `quit`.

An uncaught `abort` (-1) or `abort"` (-2) resets the engine once it
reaches the outermost call: both stacks are emptied, the definition being
compiled is abandoned, and the transient string arena is released. `ff_abort()` does the
same reset directly when nothing is running; called from inside a native
word, it raises -1 instead, so that the reset happens after everything
above it has unwound.

Any exception that ends an evaluation also abandons a definition that
evaluation was compiling (see *Compiling a definition*). The compiler's
own errors carry -22 (control structure mismatch) and -29 (`:` inside a
definition); the host sees both as `FF_ERR_MALFORMED`.

`ff->error` and `ff->error_msg` hold the most recent error raised. A
call from the host starts with them cleared, and one that succeeds
clears them again on the way out — errors that `catch` or `evaluate`
handled along the way included — so `ff_errno()`, `ff_strerror()` and
`ff_throw_code()` describe what ended the last call, and report nothing
after a successful one. An error raised with nothing running (the host
calling `ff_tracef()` itself) is recorded but not put in flight: there
is nothing to unwind, and the next call runs as usual.


## Configuration

All buffer sizes are defined in `ff_config_p.h`:

| Constant | Default | Purpose |
|---|---|---|
| `FF_STACK_SIZE` | 512 | Data and return stack depth (cells) |
| `FF_BT_STACK_SIZE` | 256 | Backtrace stack depth (frames) |
| `FF_INIT_HEAP_SIZE` | 64 | Initial heap allocation for a new word (cells) |
| `FF_PAD_INIT_SIZE` | 32 KiB | Slab size of the transient string arena |
| `FF_CF_DEPTH` | 64 | Nesting of open control structures in one definition |
| `FF_OPEN_FILES_MAX` | 32 | Streams one engine's Forth code can have open at once |
| `FF_TOKEN_SIZE` | 256 | Tokenizer token buffer (bytes) |
| `FF_ERROR_MSG_SIZE` | 512 | Error message buffer (bytes) |
| `FF_LOAD_LINE_SIZE` | 4096 | Initial line buffer for `ff_load` (bytes; longer lines grow it) |

Define `FF_32BIT` at compile time to select 32-bit cell and single-
precision float modes for constrained targets. Define
`FF_SAFE_MEM=1` to enable the address-validation pass — see *Memory
safety*. How much memory Forth code may use, and whether it may run
commands or touch files, the host sets per engine at run time — see
*Memory limits* and *Sandboxing*.

### Build-time tuning options

Beyond the buffer sizes above, these CMake options affect how the
engine is compiled:

| Option | Effect |
|---|---|
| `FF_SAFE_MEM` | Validates every address, string, execution token and file stream a word takes from the stack; see *Memory safety*. |
| `FF_WITH_SYSTEM` | ON by default. OFF leaves out the `system` word, for a build that can't run commands whatever the host allows (or a C library without `system()`). |
| `FF_WITH_FILES` | ON by default. OFF leaves out the file words (`fopen` … `stderr`) and `load`; the host's `ff_load()` stays. |
| `FF_R_TRUSTED` | Drops the `_FF_RSL` underflow checks inside opcodes the compiler emits in matched pairs (`EXIT`, `XLOOP`, `PXLOOP`, `LEAVE`, `UNLOOP`, `LOOP_I`, `LOOP_J`, and the `i +` / `r@ +` superinstructions). Well-formed code can't fail them; the compiler rejects `leave` outside a loop, but `i` or `j` misused outside one then go unchecked. Overflow checks are never dropped, since recursion depth is up to the program. The embedder-facing `FF_RSL` in custom native words is unaffected. ~5 % on loop-heavy code. A safe build (`FF_SAFE_MEM`) keeps the checks. |
| `FF_LTO` | Enables link-time optimisation (`-flto` / `/GL` via CMake's `INTERPROCEDURAL_OPTIMIZATION`). Lets the compiler inline across translation-unit boundaries — particularly `ff_exec` ↔ `ff_dict_lookup` ↔ `ff_word_native_fn`. Typically 2-5 %. |
| `FF_PGO=GENERATE` / `USE` | Profile-guided optimisation. Two-pass build: first an instrumented build that writes `*.profraw` when run against a representative workload, then `llvm-profdata merge`, then a second build with `FF_PGO=USE -DFF_PGO_DATA=path/to/merged.profdata`. Typical gain on dispatch-bound code: 5-15 %. |

Stacking `FF_R_TRUSTED=ON FF_LTO=ON` plus a PGO pass usually buys
another 10-20 % on top of the algorithmic peepholes documented in
the Performance section.


## Memory safety

The default build trusts what is on the data stack: `@`, `!`, `type`,
`execute`, `fclose` and the other words that take an address, a
string, an execution token or a file stream cast the cell to a pointer
and use it without validation. A bare `0 @` segfaults the host process
— this matches classical Forth semantics, where the language
deliberately exposes raw memory.

For embeddings that take untrusted Forth input — REPLs exposed over
the network, scripting hooks in long-lived servers, untrusted plugin
sources — that contract is wrong. *ff* offers an opt-in safe mode
controlled by a single compile-time flag. For untrusted code it goes
with the run-time limits of the next two sections and the watchdog.

### Enabling

Configure the build with `-DFF_SAFE_MEM=ON` (the CMake option) or
`-DFF_SAFE_MEM=1` (the C macro). Both forms gate the same checks; the
CMake option just propagates the macro through `add_compile_definitions`.

When the flag is off (the default), the check macros expand to
`((void)0)` and the compiler eliminates them entirely — zero text-
size and zero runtime cost.

### What gets checked

A failed check raises `FF_ERR_BAD_PTR` (-9) and unwinds like any other
error; the engine state stays consistent and later calls work
normally.

| Words | Check |
|---|---|
| `@`, `c@`, `?`, `dump` | the range read lies in a tracked region |
| `!`, `+!`, `c!`, `fgets` (its buffer) | the range written lies in a *writable* region |
| `type`, `strlen`, `strcmp`, `find`, `evaluate`, `load`, `system`, `fopen`, `fputs` | a string: its NUL comes before the end of its region |
| `s!`, `s+` | the source is a string; the destination is writable for its length |
| `,`, `c,`, `allot` | the newest word, which they extend, is a data word |
| `execute`, `catch`, deferred words, `>name`, `>body` | the xt is a word in the dictionary |
| file words | a stream the program opened itself, or `stdin` / `stdout` / `stderr` (not for `fclose`) |
| a return (`;`, `exit`), `does>`, `leave`, a loop's back edge | the return-stack cells it follows or changes are the ones the interpreter pushed for it (see *The return stack* below) |

The tracked regions are the data stack, the live part of
each string-arena slab, and every word's heap. Of the heaps, only data
words' — `create`, `variable`, `constant`, `array`, `string`, `defer`,
`does>` words (`ff_word_holds_data`) — are writable. A colon
definition's heap is its bytecode, and the definition being compiled is
bytecode in the making: a program that could write there could forge a
word pointer for `NEST` or a function for `CALL`, and take control of
the host. Bytecode can be read — `see`-style inspection and string
literals compiled into a definition both need that — but not written or
extended.

`>name` and `strerror` hand out a copy of the name or message in the
string arena, so the checks recognise it; the original is memory they
don't track (and a built-in's name may be read-only).

### The return stack

The return stack holds what the interpreter follows: return frames (an
address and the word it goes back into) and each counted loop's exit
address, among the cells a program pushes with `>r` and the loops'
limits and indexes. A program that could put its own cell where the
interpreter expects one of these would choose where execution goes —
`16 >r 16 >r` at the end of a definition used to set its return address.
So a safe build records what each return-stack cell holds, in
`ff::r_kind` — one byte a cell, set with each push (`FF_RK_DATA`,
`FF_RK_IP`, `FF_RK_WORD`, `FF_RK_LEAVE`):

- a return, and `does>` ending its defining word, take the top two
  cells only as a frame the interpreter pushed: a word, above its
  return address;
- `leave` takes the exit address only from a cell a `do` pushed as one;
- a loop's back edge changes the index in place only if the top cell is
  data, never a frame.

Anything else raises -25 (`FF_ERR_RSTACK_IMBAL`): `>r` without `r>`, or a
frame taken apart and rebuilt from data. Genuine frames still work
wherever they are, so `r> r> 2drop` still makes a word return to its
caller's caller. The return stack is not a tracked region either: `@`
and `!` don't take addresses in it. And `FF_R_TRUSTED`, which drops the
underflow checks of `exit`, `leave` and the loop words, keeps them in a
safe build. The cost is a byte store per push and a compare for each
cell checked: 1–5 % on the benchmarks, most where words are called.

The built-ins whose code carries a cell the compiler fills in — `(lit)`
and `(flit)`'s value, `(strlit)`'s string, `branch`, `?branch` and the
loop words' offsets — can't be compiled by name, in any build: there
the next word's code stood in for that cell, and `branch` jumped by an
opcode number. Run by name (at the prompt, or through `execute`) they
do nothing, since their stub has no such cell.

Word heaps are found by binary search over an index of their regions
that the arena keeps sorted as heaps are allocated, grown, trimmed and
freed, and an xt by binary search over `by_addr`: a check costs
O(log N) however the dictionary changes in between. (The index used to
be rebuilt and re-sorted after every change, so defining words and
checking addresses in turn took quadratic time.) Hot interpretive loops
slow by roughly 10-20 % under the flag; tight `@`/`!`-heavy loops slow
more.

### What is NOT covered

- **Bugs in C code embedded inside *ff*.** A miswritten native word
  that segfaults takes the host with it. Sandbox the process if that
  matters.

- **Stale pointers into live memory.** A program that keeps a pointer
  into a string it later overwrites, or an xt of a word that was
  forgotten and whose address a new word reuses, reads whatever lives
  there now. That is a logic error, not a memory-safety one: the
  address is still inside tracked memory, so nothing outside the
  engine's own regions is ever touched.

### Custom native words

When you write your own native words against `<ff_p.h>`, the check
macros are visible: `FF_CHECK_ADDR` for memory the word reads,
`FF_CHECK_WRITE` for memory it writes, `FF_CHECK_STR` for strings and
`FF_CHECK_XT` for execution tokens. Use them at the top of any word
body that consumes one from the data stack — see the *Extending*
chapter for examples.


## Memory limits

The memory Forth code makes the engine hold is counted per engine in an
`ff_mem_t` account (`ff_mem_p.h`): the slabs word storage is carved
from, the words themselves with their names, and the slabs of the
transient string arena. A host bounds it with `ff_platform::mem_limit`
(bytes; 0, the default, means no limit). Fixed-size state — the stacks
and tables — is not counted.

An allocation beyond the limit, or one the host allocator refuses,
does not crash the process. It fails softly — the heap operation that
needed it writes nothing and records the refusal in the account — and
the engine turns the record into an exception at its next check, before
anything relies on the missing memory: every word that allocates
checks right after doing so, and the outer interpreter checks before
each token. Over the limit raises -8 (`FF_ERR_HEAP_OVER`); a refusal by
the allocator raises -59 (`FF_ERR_OOM`). A definition that was being
compiled is abandoned with it, and a word that was being created is
removed. Near the limit, new slabs are cut down to what's left, so a
request that fits still succeeds.

Sizes are checked before anything is allocated: a negative count to
`allot`, `array`, `string` or `dump` (and a zero one to `allot`) raises
-24, "invalid numeric argument".

Memory comes back as well: `forget`, and a failed definition, return
their share of the word-storage arena (see *Heap and compilation*), so
code that defines and forgets words in a loop holds steady. The
transient string arena is different: its strings stay valid for the
engine's lifetime, as documented, so it only grows — until the limit,
until `ff_abort()` or an uncaught `abort` resets the engine and frees
it, or until the host calls `ff_release_strings()` between calls, once
it knows nothing still points into it.


## Sandboxing

Beyond memory, Forth code reaches outside the engine in three ways:
`system` runs a command, the file words (`fopen` and the rest, and
`stdin` / `stdout` / `stderr`) read and write files, and `load`
evaluates one. The host decides which of them a given engine allows.

- **At run time**, `ff_platform::deny` withholds any of
  `FF_CAP_SYSTEM`, `FF_CAP_FILES` and `FF_CAP_LOAD` (`FF_CAP_ALL` for
  all three). A withheld word still compiles, and raises -21
  (`FF_ERR_UNSUPPORTED`) when it runs. The host's own `ff_load()` is
  not affected: the host is trusted.
- **In between**, `ff_platform::open_file` and
  `ff_platform::run_command` replace `fopen()` and `system()`: the
  host can confine files to a directory or a list of names, serve them
  from memory (`fmemopen`, `fopencookie`), and vet or log commands.
  `fopen`, `load` and `ff_load()` all open files through the hook.
- **At build time**, `FF_WITH_SYSTEM=OFF` and `FF_WITH_FILES=OFF`
  leave the words out altogether, so the build doesn't reference
  `system()` and the file words don't exist.

Streams opened by `fopen` are tracked per engine: at most
`FF_OPEN_FILES_MAX` at once, the only ones `fclose` accepts under
`FF_SAFE_MEM`, and closed by `ff_free()` if the program left them open.

For code it doesn't trust, a host combines all of this: an
`FF_SAFE_MEM` build, `deny = FF_CAP_ALL` (or hooks that confine what is
allowed), a `mem_limit`, and a watchdog. The fuzzing harness
(`fuzz/ff_eval_fuzz.c`) runs the engine exactly so.


## Watchdog

Memory-safe mode stops a Forth program from corrupting memory; it
does not stop one from spinning forever:

~~~
: spin   begin again ;
~~~

To bound execution time, *ff* exposes a two-pronged watchdog. Both
mechanisms share the same `FF_ERR_ABORTED` error code and the same
unwind path inside `ff_exec`.

### Polling callback

`ff_platform_t::watchdog` is called periodically by the inner
interpreter — every back-branch (`AGAIN`/`UNTIL`/`REPEAT`/`LOOP`/`+LOOP`)
and every word call (`NEST`/`TNEST`) bumps an opcode counter, and
when the counter crosses `watchdog_interval` (default 65 536) the
callback fires. The callback receives the running opcode count and
returns `FF_WD_CONTINUE` or `FF_WD_ABORT`:

~~~{.c}
typedef ff_watchdog_action_t (*ff_watchdog_fn)(void *ctx,
                                               uint64_t opcodes_run);
~~~

The polling design is deliberately deterministic — same input,
same opcode count, same termination point — so the host can pick a
time-based, fuel-based, or any-other-policy decision without the
engine knowing. No signals, no threads, no platform-specific timers.

### Async abort flag

```
void ff_request_abort(ff_t *ff);
```

Stores `1` into a `volatile sig_atomic_t` field that the dispatch
loop polls at the same back-branch / call sites as the polling
callback. Safe to call from a signal handler or another thread
(per the C17 sig_atomic_t guarantees on the platforms ff targets);
no I/O, no allocation, no engine state mutation beyond the flag.
The flag is consumed when the next call from the host — `ff_eval`,
`ff_load` or `ff_exec` with nothing running — starts, so a stale
request between calls is silently ignored.

Both the flag and the opcode count belong to that outermost call. A
nested one — `evaluate` or `load` running inside a word, or a native
word's own `ff_eval` or `ff_exec` — leaves them alone; resetting them
there would let a loop around `evaluate` restart its budget, and drop a
pending request, on every pass.

### Where the check lives

A single macro `_FF_WATCHDOG_TICK()` is expanded inside the dispatch
arms of `FF_OP_NEST`, `FF_OP_TNEST`, `FF_OP_BRANCH` (when the
inline offset is negative), `FF_OP_QBRANCH` (same), `FF_OP_XLOOP`
(on the loop-back path), and `FF_OP_PXLOOP` (same). Forward
branches and straight-line opcodes don't need to check — a Forth
program can't iterate without going through one of those points.
The cost in the common case is one increment plus one branch-
predicted-not-taken per back-branch / word call.

When either signal fires, the engine clears `abort_requested` and
raises `FF_ERR_ABORTED` as THROW code -28 — the one exception `catch`
can't stop — so it unwinds to the outermost evaluation. The host's
`ff_eval` returns `FF_ERR_ABORTED` and the engine is ready for the next
call.


## Markdown rendering for terminal output

Word-manual entries (`man <word>`) and other diagnostic blocks are
authored as Markdown so the same source text can render verbatim in
the Doxygen reference, in the PDF reference manual, and in the
terminal. The terminal renderer lives in [src/ff_md.h](src/ff_md.h)
and [src/ff_md.c](src/ff_md.c), wrapping the vendored
[md4c](src/3rdparty/md4c) parser with a SAX-style callback set that
emits plain UTF-8 (or ANSI-styled UTF-8) into a snprintf-shaped
buffer.

### Two entry points, one parser

```c
int ff_md_snprintf   (char *buf, size_t size, const char *md, int width);
int ff_md_vt_snprintf(char *buf, size_t size, const char *md, int width);
```

Both share the same md4c callback set; the only difference is whether
the callbacks emit ANSI escape sequences. The `width` argument
controls word wrap (`0` disables it). Return value follows the
standard snprintf contract — number of bytes that *would have been
written*, not counting the NUL — so callers can pre-size with
`buf=NULL, size=0`, allocate `return + 1`, and call again.

The mapping from Markdown elements to ANSI codes is small and
deliberate (16-colour palette only, so the same codes work on
Windows console hosts ≥ 2018):

| Element       | Sequence                          |
|---------------|-----------------------------------|
| h1            | `\033[1;4m` … `\033[0m` (bold + underline) |
| h2-h6         | `\033[1m`  … `\033[0m`            |
| `**strong**`  | `\033[1m`  … `\033[22m`           |
| `*emphasis*`  | `\033[3m`  … `\033[23m`           |
| `` `code` ``  | `\033[36m` … `\033[39m` (cyan)    |
| code blocks   | cyan + 4-space indent             |
| `[txt](url)`  | `\033[4m` text `\033[24m` + ` (` + dim URL + `)` |
| `~~strike~~`  | `\033[9m`  … `\033[29m`           |
| `* item`      | `* ` prefix + 2-space hanging indent |
| `1. item`     | `N. ` prefix (counter increments per item) |

Tables go through the vendored
[fort](src/3rdparty/fort) library: when `MD_BLOCK_TABLE` enters,
`ff_md` allocates an `ft_table_t`; each cell's text accumulates
into a per-cell scratch buffer (via a redirect inside `emit_raw`),
gets handed to `ft_u8write` at cell-leave, and the rendered table
is emitted to the main output at table-leave. The result has
proper rounded-corner box borders, automatic column-width
calculation, and a header row in bold for the GFM `|---|---|`
separator line. ANSI escape codes inside cells are preserved
through fort.

### `FF_VT_COLORS` build flag

Both functions are always available. The compile-time flag in
`ff_config_p.h`:

```c
#if !defined(FF_VT_COLORS)
#  define FF_VT_COLORS 0
#endif
```

picks which one engine-internal callers (currently `ff_print_manual`)
reach for. Default is plain text, suitable for log files, captured
output, and non-ANSI terminals. Set to 1 when building for an
interactive terminal embedding. Custom hosts that need both at
runtime can simply call the relevant function directly — both ship in
the library regardless of the flag.
