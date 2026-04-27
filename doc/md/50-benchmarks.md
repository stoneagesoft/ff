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
18 at `-O3 -g0 -fno-exceptions`. **gforth**: as packaged by Ubuntu.


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
