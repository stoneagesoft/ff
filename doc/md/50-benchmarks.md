# Benchmarks

## Overview

This chapter compares *ff* against the three engines shipped by the
gforth distribution (currently version 0.7.3 on Debian / Ubuntu):

- **gforth-itc** — indirect-threaded engine. The simplest dispatch
  scheme; closest in spirit to *ff*'s switch-based inner interpreter.
- **gforth** — the default install. Uses direct threaded code via
  GCC's labels-as-values extension.
- **gforth-fast** — dynamic native-code translator. Roughly a JIT;
  the upper bound on what an interpreter-shaped Forth can deliver
  on this hardware without writing a real compiler.

*ff* now matches or exceeds the threaded interpreters on every
workload in this suite while remaining portable, embeddable, and
buildable under MSVC. It is not designed to compete with gforth-fast's
native-code path.


## Methodology

Each benchmark is a self-contained Forth program large enough that
process startup contributes well under 5 % of total wall-time. Every
program ends with `bye` (or `quit` for ffsh) so the engine exits
immediately after the last word. Measurements were taken with
`/usr/bin/time -f '%e'` and the **best of five back-to-back runs** is
reported, so timings reflect the engines' steady-state behaviour
rather than first-run cache misses.

All four engines were given the same source where possible. The only
difference is the recursion idiom: gforth requires `recurse` inside a
self-recursive definition, whereas *ff* resolves a word's name from
within its own body, so b3 calls `fib` directly.

**Hardware**: AMD Ryzen 7 2700X, single thread, **frequency pinned**
for these figures — `performance` governor with core boost disabled,
so every run executes at the stable 3.7 GHz base clock rather than a
variable boost state. **Build flags**: *ff* compiled with Clang
18.1.3 at `-O3 -g0 -fno-exceptions`. **gforth**: 0.7.3 as packaged
by Ubuntu (the same binary serves all three of gforth, gforth-itc,
gforth-fast — they're separate executables built from the same
release with different threading strategies).


## Workloads

| ID | Name              | What it measures                                |
|----|-------------------|-------------------------------------------------|
| b1 | empty loop        | Inner-loop dispatch overhead. 100 M iterations of `1 drop`. |
| b2 | sum               | Arithmetic-bound integer loop. `i +` over 50 M iterations. |
| b3 | fib(36)           | Recursive call/return chain (~48 M frames).     |
| b4 | variable r/m/w    | Heap-load + arithmetic + heap-store, 50 M iterations. |
| b5 | nested do-loops   | `DO/LOOP` machinery, 10 000 × 10 000 = 100 M iterations. |

Source files are reproduced verbatim in the appendix below.


## Results

Wall-clock time, milliseconds, lower is better:

| Workload         |    ffsh | gforth-itc | gforth | gforth-fast |
|------------------|--------:|-----------:|-------:|------------:|
| b1 empty loop    |     240 |        340 |    280 |         230 |
| b2 sum           |     150 |        190 |    160 |         130 |
| b3 fib(36)       |     840 |        980 |    960 |         560 |
| b4 variable r/m/w|     270 |        380 |    330 |         150 |
| b5 nested loops  |     240 |        340 |    290 |         230 |

Same numbers as ratios against ffsh (1.00 = ffsh time; smaller =
faster):

| Workload          | ffsh | gforth-itc | gforth | gforth-fast |
|-------------------|-----:|-----------:|-------:|------------:|
| b1 empty loop     | 1.00 |       1.42 |   1.17 |        0.96 |
| b2 sum            | 1.00 |       1.27 |   1.07 |        0.87 |
| b3 fib(36)        | 1.00 |       1.17 |   1.14 |        0.67 |
| b4 variable r/m/w | 1.00 |       1.41 |   1.22 |        0.56 |
| b5 nested loops   | 1.00 |       1.42 |   1.21 |        0.96 |

> **Note.** The `ffsh` column reflects the engine as shipped, including
> the stack-scope barrier: every stack-consuming word now measures
> available depth as `top - floor` rather than `top - 0` (see the *Stack
> scopes* section of `20-design.md`). The added subtraction costs roughly
> 2–3 % on stack-touching workloads — measurable on b3 and b4 and lost in
> the noise on b1/b2/b5, which spend their inner loops in peephole
> superinstructions that never reach the check. The barrier is
> unconditional (not a build flag), so it is part of every figure above;
> the workloads themselves use no scopes.
>
> These figures were captured with the CPU frequency pinned (see
> *Methodology*): `performance` governor, boost disabled, every engine
> measured back-to-back in one session at the fixed 3.7 GHz base clock.
> That removes the boost/thermal drift that makes unpinned absolutes
> wander a few percent between runs, so the numbers — not just the
> ratios — are reproducible. Expect them to read a touch higher than an
> unpinned run, which is the base clock giving up the opportunistic boost
> headroom in exchange for stability. Regenerate with `test/bench/run.sh`
> under the same pinned conditions.


## Discussion

After the peephole pass (fused `i + loop`, `<var> @`/`!`/`+!`,
`swap drop` → `nip`, `over +`, `r@ +`), the cur_word-on-frame work
that simplified diagnostics, the dictionary arena, and the trusted-
R-stack + LTO + PGO build flags, *ff* now **beats both `gforth-itc`
and the default `gforth`** on all five workloads. The margin over
`gforth-itc` is 1.17–1.42×; over the direct-threaded default
`gforth`, 1.07–1.22×. The widest gap over default gforth is b4
(1.22×), the result of variable-access peepholes collapsing the
`CREATE_RUNTIME → @` round-trip into one dispatch; the widest over
gforth-itc is the b1 / b5 dispatch loops (1.42×).

In the threaded-interpreter band — i.e. excluding `gforth-fast`'s
dynamic native-code translator — *ff* now leads on every benchmark,
including the dispatch-bound b1 / b5 where earlier releases trailed.
*ff* dispatches through a portable `switch (*ip++)` rather than the
computed-goto direct threading gforth uses, which costs per-opcode
branch prediction — but the peephole superinstructions and the
register-cached top-of-stack and instruction pointer more than pay
that back, so *ff* stays ahead of both threaded gforth engines across
the board. The only engine still faster is `gforth-fast`, and only on
the three compute-bound loops (b2, b3, b4); on b1 / b5 *ff* runs
within ~5 % of its native-code path (0.96×).

### Where the gains came from

These before → after pairs are historical captures from when each
optimization landed, taken under the older free-running governor;
they show the per-optimization delta, not the pinned absolutes in the
table above (which run a bit higher at the fixed base clock).

- **b2 sum** (290 → 120 ms): the `FF_OP_I_ADD` peephole already
  fused `i +` into one dispatch; the `FF_OP_I_ADD_LOOP` extension
  fuses `i + loop` into a single back-edge instruction. Inner-loop
  dispatch count went from 2 to 1.
- **b4 variable r/m/w** (680 → 220 ms, ~3× speedup): the
  `<var> @`/`!`/`+!` peepholes (`FF_OP_VAR_FETCH` and friends)
  bypass the `CREATE_RUNTIME` → push-address → `FETCH` round-trip.
  Each variable access is one dispatch instead of two.
- **b3 fib** (960 → 760 ms): `FF_R_TRUSTED` elides the redundant
  `_FF_RSL` checks at every NEST / EXIT, and the
  `__builtin_expect(..., 0)` hints on the validators keep the hot
  path straight-line. The 2-cell return frame (saved IP + saved
  cur_word for restoration on EXIT) costs a few percent here, paid
  for by tighter diagnostics on error.

The remaining gap to `gforth-fast` (b3: 0.67, b4: 0.56, and a near
tie at 0.96 on b1 / b5) is the cost of not having a native-code back
end. That trade is deliberate: the entire interpreter is one C source file plus
per-category dispatch includes, builds clean under MSVC, runs on
Cortex-M targets, and exposes a stable inline-C API for embedding.
A native-code translator would change all four properties.


## Comparison against native C

A useful framing for embedders evaluating *ff*: how much does the
"interpreter tax" cost compared to writing the same code in C? The
question matters because *ff*'s primary design goal is embedding —
the host already has a working C compiler, and the *ff* code is
glue, not the hot path.

The same three workloads as Forth, transcribed to C and compiled
with the same Clang 18.1.3 the *ff* binaries were built with:

~~~{.c}
/* fib(36) */
static int fib(int n) { return n < 2 ? n : fib(n-1) + fib(n-2); }
int main(void) { volatile int r = fib(36); (void)r; return 0; }

/* sum 0..49,999,999 */
int main(void) {
    volatile long sum = 0;
    for (long i = 0; i < 50000000L; i++) sum += i;
    return 0;
}

/* empty 100M iter */
int main(void) {
    volatile int x = 0;
    for (long i = 0; i < 100000000L; i++) { x = 1; (void)x; }
    return 0;
}
~~~

Wall-clock results, milliseconds, best of five:

| Benchmark            | C `-O0` | C `-O3` | ff (release) | ff vs C `-O3` |
|----------------------|--------:|--------:|-------------:|--------------:|
| empty loop (100M)    |     240 |      30 |          240 |        ~8×    |
| sum 0..49,999,999    |     130 |     120 |          150 |        ~1.3×  |
| fib(36)              |     110 |      60 |          840 |        ~14×   |

**For honest interpreter-vs-native code comparison**, the `fib` row
is the most reliable — recursion resists the dead-code elimination
that flatters the other two — and it puts *ff* at roughly **14×
slower** than `-O3` C. That is the irreducible cost of switch-
dispatched bytecode versus native machine code, and no threaded-code
Forth (gforth, gforth-itc, *ff*) closes that gap. Only
`gforth-fast`-style dynamic native-code synthesis does, at the cost
of MSVC compatibility, embeddability, and source-tree size. (The
empty-loop row reads ~8× only because `-O3` C strips the body down
to a single volatile store; against unoptimised `-O0` C, *ff* runs
the dispatch loop at parity.)

The `sum` row is misleadingly close: I had to write `volatile long
sum` to stop Clang `-O3` from eliminating the entire loop as
dead-code (the result is unused). Without the `volatile`, `-O3` C
reduces the loop to a constant — effectively infinite speed-up.
The 1.2× ratio there reflects "compiler handicapped to keep loop
running", not real-world compute.

**What this means for embedders:**

- **For host-driven control flow with occasional Forth glue**, the
  ~14× tax is invisible — time spent in C native words dominates
  whatever the script is doing. Forth coordinates; C does the work.
- **For Forth-heavy compute** (numeric inner loops, parsing, big
  string processing), expect the ~14× hit. That's still ~5-10
  Mops/sec on this Ryzen, more than enough for most embedding
  tasks (configuration, scripting, ad-hoc reports).
- **The escape hatch is custom native words.** Write the hot 5 % in
  C against `<ff_p.h>`, register through `FF_W`, and that 5 % runs
  at full C speed. The rest stays in Forth — readable, redefinable
  at runtime, hot-loaded over a network if you like. See
  [doc/md/40-extending.md](40-extending.md) for the integration
  pattern.


## Comparison against Lua

Lua is the obvious second comparison for embedders: like *ff* it
ships as a small C library, lives in-process, and is the de-facto
standard for "scripting embedded into a C/C++ host". The five
workloads transcribed verbatim to Lua 5.4:

~~~{.lua}
-- b1 empty loop
for i = 1, 100000000 do local x = 1 end

-- b2 sum
local s = 0
for i = 0, 49999999 do s = s + i end

-- b3 fib(36)
local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end
fib(36)

-- b4 variable r/m/w (1-element table to force load + store)
local v = {0}
for i = 1, 50000000 do v[1] = v[1] + 1 end

-- b5 nested loops 10 000 x 10 000
for i = 1, 10000 do
    for j = 1, 10000 do
        local x = 1
    end
end
~~~

Wall-clock, milliseconds, best of five, same hardware as the gforth
table:

| Workload          | ffsh | lua 5.4 | ratio (lua / ffsh) |
|-------------------|-----:|--------:|-------------------:|
| b1 empty loop     |  240 |     410 |              1.71× |
| b2 sum            |  150 |     230 |              1.53× |
| b3 fib(36)        |  840 |    1350 |              1.61× |
| b4 variable r/m/w |  270 |     480 |              1.78× |
| b5 nested loops   |  240 |     410 |              1.71× |

*ff* leads on every workload by a fairly uniform 1.5–1.8×. On the
arithmetic and memory workloads (b2, b3, b4) the gap tracks Lua's
per-operand type-tag dispatch (every `+` has to check whether
operands are integer, float, table-with-`__add`, or string-coerced)
and its per-call register-frame allocation — Forth has neither cost:
a cell is a cell, and call/return is push/pop on the return stack.
The pure-dispatch loops (b1, b5) show the same margin: *ff*'s
switch-threaded inner loop with peephole superinstructions turns out
to shade Lua's register VM even when the body is a no-op.

The honest caveat: this is **stock Lua**, the reference interpreter.
**LuaJIT** is a different engine entirely — a tracing JIT that
synthesises native machine code for hot loops, in the same
"different category" sense as `gforth-fast` is from `gforth`. On
these microbenchmarks LuaJIT typically lands within 2-3× of `-O3`
C, beating every threaded-code engine by an order of magnitude.
If you need that performance ceiling and can accept LuaJIT's
architectural footprint (its own assembler back end, narrower
platform support than stock Lua), that's the choice. *ff* and stock
Lua occupy the same "small portable interpreter" niche; LuaJIT
occupies the "JIT'd scripting language" niche along with
`gforth-fast`.

**What this means for embedders choosing between *ff* and stock Lua:**

- On raw VM speed, *ff* is 1.5–1.8× faster — useful but rarely the
  deciding factor.
- The deciding factors are usually language fit and footprint:
  Lua's syntax and stdlib are familiar to most teams; *ff*'s syntax
  is unfamiliar but the language is dramatically smaller (no GC,
  no closures, no metatables, no string library — a few hundred
  words against Lua's reference manual).
- For compliance / rule-engine / config-DSL embedding where every
  rule body is short and the host C does the heavy lifting through
  registered native words, both fit. Pick on syntax preference and
  audit surface area, not on these microbenchmarks.


## Comparison against Python

Python is the other ubiquitous embeddable scripting language in the
C ecosystem. CPython 3 is the natural counterpart to stock Lua —
same niche, same lack of a JIT in the default install, same role as
"glue for a C host". The five workloads transcribed verbatim to
Python 3:

~~~{.py}
# b1 empty loop
for i in range(100000000):
    x = 1

# b2 sum
s = 0
for i in range(50000000):
    s += i

# b3 fib(36)
def fib(n):
    if n < 2: return n
    return fib(n - 1) + fib(n - 2)
fib(36)

# b4 variable r/m/w (1-element list to force load + store)
v = [0]
for i in range(50000000):
    v[0] = v[0] + 1

# b5 nested loops 10 000 x 10 000
for i in range(10000):
    for j in range(10000):
        x = 1
~~~

Wall-clock, milliseconds, same hardware and pinned session as the
gforth table. CPython 3.12.3 (the system Python on Ubuntu 24.04). The
ffsh column is the same pinned capture as the main results table
above; the Python column was measured back-to-back with it:

| Workload          | ffsh | python 3.12 | ratio (py / ffsh) |
|-------------------|-----:|------------:|------------------:|
| b1 empty loop     |  240 |        5700 |             23.8× |
| b2 sum            |  150 |        5020 |             33.5× |
| b3 fib(36)        |  840 |        2750 |              3.3× |
| b4 variable r/m/w |  270 |        4330 |             16.0× |
| b5 nested loops   |  240 |        5720 |             23.8× |

CPython sits a tier below stock Lua on these microbenchmarks (Lua
itself runs the b1/b5 dispatch workloads at ~410 ms here — Python is
~14× slower than Lua on those, ~2× slower on fib). Two structural
factors dominate:

- **Per-bytecode object overhead.** CPython integers are heap-
  allocated `PyObject`s with refcounts; every `i + 1` allocates a
  result object (or hits the small-int cache), increments two
  refcounts, and dispatches through the `__add__` slot. The b2 sum
  and b4 r/m/w workloads spend most of their time in this object-
  protocol machinery, not in arithmetic.
- **No tail-call / no recursion fast path.** b3 fib is the closest
  ratio (3.3×) because both engines do straight call/return with
  a frame allocation per invocation, and CPython's frame allocator
  is well-tuned. The arithmetic per call is negligible compared to
  the call overhead, so CPython's per-op tax doesn't dominate here.

The honest caveat, same as for Lua: this is **stock CPython**, the
reference interpreter. **PyPy** is a different engine — a tracing
JIT that synthesises native machine code for hot loops, comparable
to `gforth-fast` and LuaJIT. On these microbenchmarks PyPy
typically lands within 2-5× of `-O3` C, beating every threaded-
code engine. If your embedding constraints allow PyPy (its own
substantial runtime, slower C-extension interop than CPython,
narrower platform support), that's the choice. *ff* and stock
CPython occupy the same "small portable interpreter" niche.

**What this means for embedders choosing between *ff* and stock CPython:**

- On raw VM speed, *ff* is 16-34× faster on integer / dispatch
  workloads, 3× faster on call-bound recursion. For Forth-heavy
  compute, this is a real gap.
- The deciding factor is almost never raw speed — it's the host
  ecosystem and team familiarity. Python's stdlib is enormous and
  its C API is the de-facto interop layer for ML, scientific,
  and data tooling. *ff* offers none of that breadth.
- For embedded / footprint-constrained hosts the calculus flips:
  *ff* is one library plus a few hundred KB, no GC, no thread
  state machinery, no GIL, no import system, no `.pyc` cache
  directory in the user's filesystem. CPython embedding pulls in
  the entire interpreter and its runtime services whether you
  use them or not.
- For Forth-style control DSLs (state machines, rule engines,
  pipeline-step glue) where the host C does the heavy lifting,
  *ff*'s niche is intact regardless of what Python does on a
  microbenchmark.


## Reproducing the numbers

The `test/bench/` directory ships the sources verbatim. To re-run on
your own hardware:

~~~{.sh}
cd test/bench
./run.sh   # prints the table above
~~~

The script measures the best of five runs of each benchmark against
each engine, formats the results identically to this chapter, and
takes about a minute to complete on the reference hardware.

The C transcriptions sit next to the Forth ones as `c_b1.c`,
`c_b2.c`, `c_b3.c`. Build and run them by hand:

~~~{.sh}
cd test/bench
for src in c_b1 c_b2 c_b3; do
    clang -O3 $src.c -o $src
    /usr/bin/time -f '%e' ./$src
done
~~~

The Lua transcriptions sit alongside as `b1.lua` ... `b5.lua`. Run
them with a stock Lua 5.4:

~~~{.sh}
cd test/bench
for n in 1 2 3 4 5; do
    /usr/bin/time -f '%e' lua5.4 b$n.lua
done
~~~

The Python transcriptions sit alongside as `b1.py` ... `b5.py`. Run
them with stock CPython 3:

~~~{.sh}
cd test/bench
for n in 1 2 3 4 5; do
    /usr/bin/time -f '%e' python3 b$n.py
done
~~~

`run.sh` autodetects `lua5.4` and `python3` on `PATH` and appends
the corresponding columns when present, so the script above is just
for re-running a single engine in isolation.


## Appendix: benchmark sources

**b1 — empty counting loop (100 M iterations).**

~~~
: bench  100000000 0 do  1 drop  loop ;
bench  bye
~~~

**b2 — sum of 0 .. N-1, N = 50 M.**

~~~
: bench  0 50000000 0 do  i +  loop ;
bench  drop  bye
~~~

**b3 — recursive `fib(36)`. gforth uses `recurse`; *ff* calls the word
by name from inside its own body.**

~~~
: fib dup 2 < if exit then dup 1 - recurse swap 2 - recurse + ;
36 fib drop bye
~~~

**b4 — variable read/modify/write, 50 M iterations.**

~~~
variable v   0 v !
: bench  50000000 0 do  v @  1 +  v !  loop ;
bench  v @ drop  bye
~~~

**b5 — nested `DO/LOOP`, 10 000 × 10 000 = 100 M iterations.**

~~~
: bench  10000 0 do  10000 0 do  1 drop  loop  loop ;
bench  bye
~~~
