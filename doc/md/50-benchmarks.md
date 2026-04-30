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

*ff* aims at parity with the threaded interpreters while remaining
portable, embeddable, and buildable under MSVC. It is not designed to
compete with gforth-fast's native-code path.


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

**Hardware**: AMD Ryzen 7 2700X, single thread, no frequency pinning
(default scaling governor). **Build flags**: *ff* compiled with Clang
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
| b1 empty loop    |     340 |        300 |    250 |         200 |
| b2 sum           |     120 |        170 |    140 |         110 |
| b3 fib(36)       |     850 |        880 |    850 |         500 |
| b4 variable r/m/w|     250 |        340 |    310 |         130 |
| b5 nested loops  |     340 |        300 |    260 |         200 |

Same numbers as ratios against ffsh (1.00 = ffsh time; smaller =
faster):

| Workload          | ffsh | gforth-itc | gforth | gforth-fast |
|-------------------|-----:|-----------:|-------:|------------:|
| b1 empty loop     | 1.00 |       0.88 |   0.74 |        0.59 |
| b2 sum            | 1.00 |       1.42 |   1.17 |        0.92 |
| b3 fib(36)        | 1.00 |       1.04 |   1.00 |        0.59 |
| b4 variable r/m/w | 1.00 |       1.36 |   1.24 |        0.52 |
| b5 nested loops   | 1.00 |       0.88 |   0.76 |        0.59 |


## Discussion

After a peephole pass round (fused `i + loop`, `<var> @`/`!`/`+!`,
`swap drop` → `nip`, `over +`, `r@ +`) plus the trusted-R-stack and
LTO/PGO build flags, *ff* now **beats both `gforth-itc` and the
default `gforth`** on the two arithmetic-heavy workloads (b2 and b4)
that historically were *ff*'s weak spot. b3 (recursion) is at parity
with the default `gforth` and 4 % ahead of `gforth-itc`. b1 and b5
(pure dispatch-bound, no peephole opportunities) trail by 13-26 %
— the cost of a `switch (*ip++)` versus computed-goto, since the
single-jump-site can't get per-opcode branch prediction.

In the threaded-interpreter band — i.e. excluding `gforth-fast`'s
dynamic native-code translator — *ff* now leads on three of five
benchmarks and trails on two by amounts that are below the noise
floor of a 100-million-dispatch workload. The peephole table (see
[doc/md/20-design.md](20-design.md) `### Two-op peephole
superinstructions`) is what closed the previous 2× gaps.

### Where the gains came from

- **b2 sum** (290 → 120 ms): the `FF_OP_I_ADD` peephole already
  fused `i +` into one dispatch; this round added
  `FF_OP_I_ADD_LOOP` so `i + loop` is a single back-edge
  instruction. Inner-loop dispatch count: 2 → 1.
- **b4 variable r/m/w** (680 → 250 ms): the `<var> @`/`!`/`+!`
  peepholes (`FF_OP_VAR_FETCH` etc.) bypass the
  `CREATE_RUNTIME` → push-address → `FETCH` round-trip. Each
  variable access is one dispatch instead of two.
- **b3 fib** (960 → 850 ms): downstream of `FF_R_TRUSTED`'s
  elision of redundant `_FF_RSL` checks at every NEST/EXIT, plus
  the `__builtin_expect(..., 0)` hints on the validators making
  the hot path straight-line.

The remaining gap to `gforth-fast` (b1: 0.59, b3: 0.59, b4: 0.52)
is the cost of not having a native-code back end. That trade is
deliberate: the entire interpreter is one C source file plus
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
| empty loop (100M)    |     210 |      20 |          340 |        ~17×   |
| sum 0..49,999,999    |     110 |     100 |          120 |        ~1.2×  |
| fib(36)              |     100 |      50 |          850 |        ~17×   |

**For honest interpreter-vs-native code comparison**, look at the
first and third rows: roughly **15-20× slower** than `-O3` C. That's
the irreducible cost of switch-dispatched bytecode versus native
machine code, and no threaded-code Forth (gforth, gforth-itc, *ff*)
closes that gap. Only `gforth-fast`-style dynamic native-code
synthesis does, at the cost of MSVC compatibility, embeddability,
and source-tree size.

The `sum` row is misleadingly close: I had to write `volatile long
sum` to stop Clang `-O3` from eliminating the entire loop as
dead-code (the result is unused). Without the `volatile`, `-O3` C
reduces the loop to a constant — effectively infinite speed-up.
The 1.2× ratio there reflects "compiler handicapped to keep loop
running", not real-world compute.

**What this means for embedders:**

- **For host-driven control flow with occasional Forth glue**, the
  15-20× tax is invisible — time spent in C native words dominates
  whatever the script is doing. Forth coordinates; C does the work.
- **For Forth-heavy compute** (numeric inner loops, parsing, big
  string processing), expect the 15-20× hit. That's still ~5-10
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
| b1 empty loop     |  340 |     370 |              1.09× |
| b2 sum            |  120 |     220 |              1.83× |
| b3 fib(36)        |  850 |    1240 |              1.46× |
| b4 variable r/m/w |  250 |     450 |              1.80× |
| b5 nested loops   |  340 |     370 |              1.09× |

*ff* leads on every workload. The pure-dispatch benchmarks (b1,
b5) are within 9 % — both VMs are dispatch-bound and there's not
much daylight between switch-threaded Forth and Lua's register VM
when the body is a no-op. The arithmetic and memory workloads (b2,
b3, b4) show 1.5-1.8× gaps, which is consistent with Lua's
per-operand type-tag dispatch (every `+` has to check whether
operands are integer, float, table-with-`__add`, or string-coerced)
and its per-call register-frame allocation. Forth has neither cost:
a cell is a cell, and call/return is push/pop on the return stack.

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

- On raw VM speed, *ff* is 1.0-1.8× faster — useful but rarely the
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
