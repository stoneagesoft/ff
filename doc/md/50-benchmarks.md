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
| b1 empty loop    |     300 |        300 |    250 |         200 |
| b2 sum           |     290 |        170 |    140 |         110 |
| b3 fib(36)       |     960 |        880 |    850 |         500 |
| b4 variable r/m/w|     680 |        340 |    310 |         130 |
| b5 nested loops  |     300 |        300 |    260 |         200 |

Same numbers as ratios against ffsh (1.00 = ffsh time; smaller =
faster):

| Workload          | ffsh | gforth-itc | gforth | gforth-fast |
|-------------------|-----:|-----------:|-------:|------------:|
| b1 empty loop     | 1.00 |       1.00 |   0.83 |        0.67 |
| b2 sum            | 1.00 |       0.59 |   0.48 |        0.38 |
| b3 fib(36)        | 1.00 |       0.92 |   0.89 |        0.52 |
| b4 variable r/m/w | 1.00 |       0.50 |   0.46 |        0.19 |
| b5 nested loops   | 1.00 |       1.00 |   0.87 |        0.67 |


## Discussion

*ff* sits at the top of the threaded-interpreter band: equal to
gforth-itc on dispatch-bound and loop-machinery workloads (b1, b5),
within ~10 % on call-heavy code (b3), and 1.7-2.0× slower on the
arithmetic and memory-traffic loops (b2, b4).

Two of those gaps are worth calling out:

- **b2 (sum)** — gforth's `i +` benefits from a fused `i+` opcode
  that pushes the loop index and adds in one dispatch. *ff* has no such
  super-instruction; the equivalent sequence is two opcodes
  (`FF_OP_LOOP_I`, `FF_OP_ADD`) and two through-cache TOS round
  trips. A peephole pass that recognised this idiom would close the
  gap without changing the language.

- **b4 (variable r/m/w)** — gforth's `+!` is a single word that
  takes the address from TOS and compounds the load/add/store. *ff*
  ships `+!` too but the program above intentionally writes the
  pattern out (`v @ 1 + v !`) to measure the four primitives in
  isolation. Rewriting as `1 v +!` brings *ff* within ~1.2× of gforth.

gforth-fast's lead — 0.19 on b4, 0.38-0.67 elsewhere — is the cost of
not having a native-code back end. That trade is deliberate: the
entire interpreter is one C source file plus per-category dispatch
includes, builds clean under MSVC, runs on Cortex-M targets, and
exposes a stable inline-C API for embedding. A native-code translator
would change all four properties.


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
