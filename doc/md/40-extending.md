# Extending *ff* with Custom C Words

## Overview

*ff* is designed to be embedded and extended. A typical use case is a
C application that wants to expose some of its own functionality to a
user-facing Forth shell: a graphics library gets `draw-line` words, a
database gets `query` words, an embedded controller gets `servo!`
words.

This chapter walks through adding a new native C word end-to-end.
Everything you need is in the installed header set — no patching of the
library is required.


## A minimal custom word

Let's add a word `square` that pops one integer and pushes its square.

**Step 1 — include the engine internals.** Custom native words need the
stack access macros and the `FF_W` definition macros, both of which live
under the `_p.h` internals umbrella:

~~~{.c}
#include <ff_p.h>
~~~

That single include pulls in the public `ff.h`, `ff_platform.h`, and
`ff_error.h` plus every private `_p.h` layout. Everything below is now
available.

**Step 2 — write the word.** Native words are plain C functions with
signature `void (*)(ff_t *)`:

~~~{.c}
static void ff_w_square(ff_t *ff)
{
    FF_SL(ff, 1);              /* require 1 item on the data stack */
    ff_int_t x = *ff_s0(ff);   /* read top of stack */
    *ff_s0(ff) = x * x;        /* replace with square */
}
~~~

- `FF_SL(ff, 1)` checks that the data stack has at least one item. If
  not, it raises `FF_ERR_STACK_UNDER` via `ff_tracef` and `return`s
  immediately — no further code in this function executes.
- `ff_s0(ff)` returns a pointer to the top-of-stack cell. Dereference
  with `*` to read or write. The compiler inlines this away at `-O1+`,
  so the generated machine code is identical to the engine's own opcode
  handlers.
- Here the word consumes 1 cell and produces 1 cell — the net stack
  delta is zero, so no `FF_SO` overflow check is needed.

**Step 3 — register the word in a table.** Words are registered as
entries in an `ff_word_def_t` array:

~~~{.c}
const ff_word_def_t MY_WORDS[] =
{
    FF_W("square", ff_w_square,
         "( n -- n*n )  Square\n"
         "Pops the top of stack and pushes its square."),
    FF_WEND
};
~~~

- `FF_W` is the non-immediate variant; `FF_WI` marks a word that
  executes at compile time (like `IMMEDIATE`).
- External natives always go through the `FF_OP_CALL` escape hatch —
  a compiled caller emits the two-cell sequence `FF_OP_CALL, fn_ptr`.
  Real opcode assignments are reserved for the engine's built-ins;
  embedders cannot add an opcoded word.
- The help text (optional, may be `NULL`) is shown by the `man` word;
  the first line before the `\n` is the signature, the rest is prose.
- The array is terminated with `FF_WEND`.

**Step 4 — wire the table into the dictionary after `ff_new`:**

~~~{.c}
ff_t *ff = ff_new(&platform);
ff_dict_define(&ff->dict, MY_WORDS);
~~~

That's it. The word is immediately callable from any subsequent
`ff_eval()`:

~~~
▶ 7 square .
49
~~~


## Stack-effect conventions

A word that pops `p` items and pushes `q` items has stack effect
`( -p +q )`. Use the validation macros at the top of the body:

| Effect | Validators to add |
|---|---|
| `( -p +q )`, `q ≤ p` | `FF_SL(ff, p)` only — stack shrinks, no overflow possible |
| `( -p +q )`, `q > p` | `FF_SL(ff, p)` **and** `FF_SO(ff, q - p)` |
| `( -- +q )` | `FF_SO(ff, q)` only |
| Uses return stack: `( R: -p +q )` | `FF_RSL(ff, p)`, `FF_RSO(ff, q)` |
| Compile-time only | `FF_COMPILING(ff)` |

All five macros early-return on failure. Put them before any stack
manipulation — once you have written to a cell, rolling back is on you.


## Pointer and xt validation

Beyond the stack checks, *ff* exposes two macros for native words that
take an *address* or an *execution token* off the stack and dereference
it. Use them whenever the value originated from Forth code, since a
buggy or hostile script can put any integer into that slot:

| Macro | Use when |
|---|---|
| `FF_CHECK_ADDR(ff, addr, bytes)` | The word reads or writes `bytes` of memory at `addr`. Confirms the range falls inside one of the engine's tracked regions (any word's heap, the data / return stacks, the pad ring). |
| `FF_CHECK_XT(ff, w)` | The word receives an `ff_word_t *` from Forth code (e.g. as the target of a custom `execute`-like primitive) and is about to dispatch through it. Confirms the pointer is a live dictionary entry. |

Both expand to a runtime check + `FF_ERR_BAD_PTR` raise + `return`
when the engine was built with `-DFF_SAFE_MEM=ON`, and to `((void)0)`
otherwise. So the same source ships in both build modes — the costed
checks materialise only where needed. The cost is one function call
plus an O(N) walk over the dictionary; the dispatch primitives in the
engine's own opcode set use exactly the same macros, so a custom
native word incurs the same per-call overhead as a built-in.

A pointer-consuming word looks like this:

~~~{.c}
/* `peek` ( a -- v )  — fetch a single cell from address a. */
static void ff_w_peek(ff_t *ff)
{
    FF_SL(ff, 1);
    ff_int_t addr = *ff_s0(ff);
    FF_CHECK_ADDR(ff, (const void *)(intptr_t)addr, sizeof(ff_int_t));
    *ff_s0(ff) = *(ff_int_t *)(intptr_t)addr;
}
~~~

An xt-consuming word follows the same pattern:

~~~{.c}
/* `run-twice` ( xt -- )  — execute the word identified by xt, twice. */
static void ff_w_run_twice(ff_t *ff)
{
    FF_SL(ff, 1);
    ff_word_t *w = (ff_word_t *)(intptr_t)*ff_s0(ff);
    FF_CHECK_XT(ff, w);
    ff_stack_pop(&ff->stack);
    ff_exec(ff, w);
    ff_exec(ff, w);
}
~~~

Two practical guidelines:

- **Place the check before the first dereference, not before the first
  stack pop.** If the check fails it `return`s immediately, leaving the
  arguments on the stack. The host loop then prints the error and the
  user can inspect the stack. Popping first would discard them.
- **`FF_CHECK_ADDR` validates the entire `[addr, addr + bytes)` range
  in one call** — pass the actual size you intend to dereference
  (`sizeof(ff_int_t)`, `1` for a byte, the source `strlen+1` for a
  string copy). One macro call covers the whole span.

If your embedder always builds with `FF_SAFE_MEM=ON`, you can also
call `ff_addr_valid(ff, addr, bytes)` and `ff_word_valid(ff, w)`
directly — they're public helpers regardless of build mode and let
you implement custom error-reporting paths (e.g., raising a domain
error instead of `FF_ERR_BAD_PTR`).


## Stack access

Reading and writing cells uses the `ff_s*` / `ff_r*` accessors, all
pointer-returning inlines:

~~~{.c}
*ff_s0(ff)    /* top of data stack */
*ff_s1(ff)    /* next */
*ff_s2(ff)    /* and so on through ff_s5 */
*ff_r0(ff)    /* top of return stack */
*ff_r1(ff)    /* next */

*ff_sat(&ff->stack, n)   /* arbitrary depth on either stack */
*ff_sat(&ff->r_stack, n)
~~~

Pushing and popping uses the inline primitives in `ff_stack_p.h`:

~~~{.c}
ff_stack_push     (&ff->stack, v);     /* push integer */
ff_stack_push_ptr (&ff->stack, ptr);   /* push pointer */
ff_stack_push_real(&ff->stack, 3.14);  /* push double */
ff_stack_pop      (&ff->stack);        /* pop, returns ff_int_t */
ff_stack_popn     (&ff->stack, n);     /* drop n items */
~~~

For floats, prefer the convenience forms that cast through the top-of-
stack automatically:

~~~{.c}
ff_real_t x = ff_real0(ff);   /* read TOS as double */
ff_set_real0(ff, x * x);      /* write TOS as double */
~~~


## Error reporting

Use `ff_tracef` to report errors. The first argument is the engine; the
second is a packed severity-plus-code value; the remainder is a printf
format string:

~~~{.c}
ff_tracef(ff, FF_SEV_ERROR | FF_ERR_APPLICATION,
          "bad input: %d", x);
return;
~~~

Available severities are in `ff_error.h`: `FF_SEV_TRACE`, `FF_SEV_DEBUG`,
`FF_SEV_INFO`, `FF_SEV_WARNING`, `FF_SEV_ERROR`. Error codes include
`FF_ERR_APPLICATION` (use this for your own domain errors),
`FF_ERR_STACK_UNDER`, `FF_ERR_DIV_ZERO`, `FF_ERR_UNDEFINED`, and more.
Only `FF_SEV_ERROR` makes `ff_eval` stop; lower severities are informational.

After calling `ff_tracef` with an `FF_SEV_ERROR` level, return from the
word — the state is marked as errored and the interpreter will unwind.


## Complete example

A single-file example adding a `clamp` word `( n lo hi -- n' )`:

~~~{.c}
#include <ff_p.h>

#include <stdio.h>
#include <stdlib.h>


static void ff_w_clamp(ff_t *ff)
{
    FF_SL(ff, 3);

    ff_int_t hi = *ff_s0(ff);
    ff_int_t lo = *ff_s1(ff);
    ff_int_t n  = *ff_s2(ff);

    if (lo > hi)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_APPLICATION,
                  "clamp: lo (%ld) > hi (%ld)", (long)lo, (long)hi);
        return;
    }

    if      (n < lo) n = lo;
    else if (n > hi) n = hi;

    ff_stack_popn(&ff->stack, 2);   /* drop hi, lo */
    *ff_s0(ff) = n;                 /* overwrite original n */
}

static const ff_word_def_t MY_WORDS[] =
{
    FF_W("clamp", ff_w_clamp,
         "( n lo hi -- n' )  Clamp n to [lo, hi]."),
    FF_WEND
};

static int plain_vprintf(void *ctx, const char *fmt, va_list args)
{
    (void)ctx;
    return vprintf(fmt, args);
}

int main(void)
{
    ff_platform_t platform =
    {
        .context = NULL,
        .vprintf = plain_vprintf,
        .vtracef = NULL
    };
    ff_t *ff = ff_new(&platform);
    ff_dict_define(&ff->dict, MY_WORDS);

    ff_eval(ff, "15 0 10 clamp .\n");   /* -> 10 */
    ff_eval(ff, "-5 0 10 clamp .\n");   /* ->  0 */
    ff_eval(ff, " 7 0 10 clamp .\n");   /* ->  7 */

    ff_free(ff);
    return 0;
}
~~~

Build against the installed library:

~~~
cc -I/opt/ff/include/ff my_ext.c -L/opt/ff/lib -lff_static -lm
~~~


## Bounding execution time (the watchdog)

A Forth program can hang the host:

~~~
: spin   begin again ;
spin
~~~

For embeddings that take untrusted scripts (REPLs over the network,
plugin sources, sandboxed automation), *ff* exposes two complementary
mechanisms — pick whichever fits your hosting model.

### Polling watchdog (deterministic)

Set `ff_platform_t::watchdog` and `watchdog_interval`. The inner
interpreter polls the callback at every back-branch (`AGAIN`,
`UNTIL`, `REPEAT`, `LOOP`, `+LOOP`) and at every word call (`NEST`,
`TNEST`). The callback receives a running opcode count and decides
whether to keep going:

~~~{.c}
#include <time.h>

typedef struct {
    clock_t deadline;
} my_ctx;

static ff_watchdog_action_t my_watchdog(void *ctx, uint64_t opcodes_run)
{
    (void)opcodes_run;
    my_ctx *c = (my_ctx *)ctx;
    return (clock() >= c->deadline) ? FF_WD_ABORT : FF_WD_CONTINUE;
}

int main(void)
{
    my_ctx c = { .deadline = clock() + CLOCKS_PER_SEC };  /* 1 s */
    ff_platform_t p = {
        .context           = &c,
        .vprintf           = my_vprintf,
        .watchdog          = my_watchdog,
        .watchdog_interval = 65536,   /* poll every 64 K opcodes */
    };
    ff_t *ff = ff_new(&p);
    ff_eval(ff, "...");                /* returns FF_ERR_ABORTED on timeout */
    ff_free(ff);
}
~~~

`watchdog_interval = 0` picks the engine default (65 536 opcodes,
which on this hardware is roughly 100-200 µs of latency). The
counter resets to zero on every fresh `ff_eval` call.

The cost is one branch per back-branch / call site when the
callback is registered, and one indirect call every N opcodes when
N is reached — typically below 1 % overhead on real workloads.

### Async abort flag (preemptive)

Sometimes the "stop now" signal arrives outside the callback's
sight: an alarm signal, a UI cancel button, a watchdog thread
counting down a hard wall-clock budget. For those, call
`ff_request_abort(ff)` from anywhere — including inside a signal
handler, since the implementation is one `sig_atomic_t` store with
no I/O and no other state mutation. The dispatch loop picks up the
flag at the next back-branch / word call and unwinds the same way
the polling watchdog does.

~~~{.c}
#include <signal.h>

static ff_t *g_ff;   /* set by main, read by handler — single thread */

static void on_alarm(int sig)
{
    (void)sig;
    ff_request_abort(g_ff);
}

int main(void)
{
    g_ff = ff_new(&platform);
    signal(SIGALRM, on_alarm);
    alarm(5);                         /* 5 s budget */
    ff_eval(g_ff, untrusted_source);  /* returns FF_ERR_ABORTED on timeout */
    alarm(0);
    ff_free(g_ff);
}
~~~

The two paths share `FF_ERR_ABORTED` and the same unwind machinery,
so a host can install both — the polling callback handles the
common-case time budget, `ff_request_abort` handles the long-tail
"something else fired" case. Both leave the engine in a clean state
ready for the next `ff_eval` call.


## Tips

- **Read the built-ins.** The 16 files under `src/words/*.c` cover every
  idiom: pure stack words (`words/ff_words_stack.c`), compile-time words
  (`words/ff_words_comp.c`), control flow (`words/ff_words_ctrl.c`),
  I/O (`words/ff_words_conio.c`), file access (`words/ff_words_file.c`).
  They all use the same macros this chapter describes.
- **Conservative overflow checks.** If your word has a complex
  intermediate push/pop sequence, `FF_SO(ff, max_intermediate)` is safer
  than `FF_SO(ff, net_delta)`. Being one cell too pessimistic just
  errors earlier on a nearly-full stack; being too optimistic corrupts
  memory.
- **Don't touch `ff->ip`, `ff->state`, or `ff->dict` directly** unless
  you understand the inner interpreter. Use existing words (call
  `ff_dict_lookup`, then set up and call `ff_exec`) when you need to
  chain to other Forth code.
- **Layouts can change.** The `_p.h` headers are part of the installed
  surface but are marked as advanced internals. Treat struct layouts as
  subject to change across releases, and prefer the accessor inlines
  over raw field access.
