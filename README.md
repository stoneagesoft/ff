# Fotissimo

A compact, embeddable Forth interpreter written in ISO C17.

*ff* is delivered as a library: the host wires up I/O callbacks through
`ff_platform_t` and drives the interpreter via `ff_eval()`. There is no
global state, no `printf` calls inside the engine, no platform-specific
threading code, and no hard dependency outside the C standard library.
A vendored copy of the Forth shell `ffsh` lives under
[`examples/ffsh/`](examples/ffsh/) and shows the embedding boilerplate
end-to-end.


## Highlights

- **Embeddable.** One `ff_t *` per interpreter instance; multiple
  instances coexist freely. All output flows through caller-supplied
  `vprintf`/`vtracef` callbacks — embed in environments that have no
  `stdout`.
- **Portable.** Builds clean under GCC, Clang, and MSVC; no compiler
  extensions on the hot path. Tested on Linux x86-64 and aarch64; a
  WebAssembly build target is supported.
- **Fast.** Token-threaded bytecode dispatched through a single
  `switch (*ip++)` whose case bodies are `#include`d from per-category
  headers. Top-of-stack and instruction pointer are register-cached
  for the duration of `ff_exec`. Roughly parity with
  [gforth-itc](https://gforth.org/) on the [bundled benchmark
  suite](doc/md/50-benchmarks.md), within ~10 % of plain `gforth` on
  call-heavy workloads, and 1.7-2× slower on arithmetic-heavy loops
  where gforth wins on fused super-instructions.
- **~130 opcodes** covering integer / floating-point math, stack
  manipulation, control flow, counted loops, `create`/`does>`,
  `defer`/`is`, dictionary introspection, file I/O, and string
  handling. Specialised literal and arithmetic forms (`FF_OP_LIT0`,
  `LITADD`, `INC2`, `MUL2`, …) emitted by the compiler's peephole
  pass.
- **Per-word heaps.** Each definition owns its compiled body — there
  is no global heap or `HERE` pointer. Words can grow in place
  without invalidating any other word's compiled-in addresses.
- **Optional memory safety.** A `FF_SAFE_MEM` build flag turns every
  `@`/`!`/`+!`/`c@`/`c!`/`s!`/`s+`/`strlen`/`strcmp`/`execute` into a
  bounds-checked operation against the engine's tracked regions. Off
  by default — when on, the cost is roughly 10-20 % on dispatch-bound
  code.
- **Strings are C strings.** No counted-string convention; every
  string-valued word returns or accepts a single `char *` to a NUL-
  terminated sequence. `s!`, `s+`, `strlen`, `strcmp` are one-line
  wrappers around `<string.h>`.
- **Pretty decompiler.** `see name` recovers the source-level
  control flow (`if`/`else`/`then`, `begin`/`while`/`repeat`,
  `do`/`loop`, …) and prints the body indented:

  ```
  : nested-if
      dup 0 > if
          dup 5 < if
              1 .
          else
              2 .
          then
      else
          3 .
      then
  ;
  ```


## Quick start

```sh
# Build the library and the example shell.
cmake -B build -DFF_BUILD_SHARED=ON
cmake --build build -j

# Run the shell.
./build/examples/ffsh/ffsh
▶ : square dup * ;
▶ 7 square .
49
```

A trivial host program embedding the engine:

```c
#include <ff.h>
#include <ff_platform.h>
#include <stdio.h>

static int my_vprintf(void *ctx, const char *fmt, va_list args)
{
    (void)ctx;
    return vprintf(fmt, args);
}

int main(void)
{
    ff_platform_t p = { .vprintf = my_vprintf };
    ff_t *ff = ff_new(&p);
    ff_eval(ff, ": cube dup dup * * ;\n");
    ff_eval(ff, "5 cube .\n");          /* prints "125" */
    ff_free(ff);
}
```

Build it against an installed *ff*:

```sh
cc -lff main.c -o my_app
```

Or pick *ff* up via CMake:

```cmake
find_package(ff 1.0 REQUIRED)
target_link_libraries(my_app PRIVATE ff::ff)
```


## Building from source

Requires CMake ≥ 3.10, a C17 compiler, and `make`. Optional: `ninja`,
`doxygen`, and a TeX distribution if you want to rebuild the
reference manual.

```sh
cmake -B build                      # default: static archive only
cmake -B build -DFF_BUILD_SHARED=ON # also build libff.so
cmake --build build -j
sudo cmake --install build          # /usr/local by default
```

Configurable options:

| Option                 | Default | Effect                                            |
|------------------------|---------|---------------------------------------------------|
| `FF_BUILD_STATIC`      | ON      | Build `libff_static.a`                            |
| `FF_BUILD_SHARED`      | OFF     | Build the shared library `libff.so.1.0.0`         |
| `FF_BUILD_TESTS`       | OFF     | Build the regression-test driver and register it with `ctest` |
| `FF_SAFE_MEM`          | OFF     | Validate every fetch/store/execute pointer (see below) |
| `BUILD_SHARED_LIBS`    | OFF     | Shorthand: equivalent to `FF_BUILD_SHARED=ON`     |

Run the test suite:

```sh
cmake -B build -DFF_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```


## Installing

### Debian / Ubuntu

The repository ships a `debian/` directory that produces three
packages:

| Package      | Contents                                                            |
|--------------|---------------------------------------------------------------------|
| `libff1`     | Shared runtime library (`libff.so.1`, `libff.so.1.0.0`).            |
| `libff-dev`  | Static archive, headers under `/usr/include/ff/`, CMake package config (`find_package(ff)`). |
| `libff-doc`  | Doxygen HTML manual + the typeset `ff-reference.pdf`.               |

Build the `.deb`s from the repository root:

```sh
./build-deb.sh
```

The output `.deb` files land in the parent directory.


## Documentation

The full reference manual lives under [`doc/md/`](doc/md/):

| Chapter                                              | Topic                                                   |
|------------------------------------------------------|---------------------------------------------------------|
| [`20-design.md`](doc/md/20-design.md)                | Architecture: cell model, engine struct, opcode set, inner interpreter, dictionary, performance optimisations, memory-safety mode. |
| [`30-codestyle.md`](doc/md/30-codestyle.md)          | C17 conventions used inside the engine.                 |
| [`40-extending.md`](doc/md/40-extending.md)          | Adding native C words: stack-effect macros, address validators, complete examples. |
| [`50-benchmarks.md`](doc/md/50-benchmarks.md)        | Comparison against the three gforth engines on five workloads. |

Build the HTML and PDF reference manual:

```sh
cd doc && make pdf
```

Outputs `doc/html/` (Doxygen tree) and `doc/ff-YYYYMMDD.pdf`.


## Memory-safety mode

The default build trusts addresses on the data stack: a bare `0 @`
segfaults the host process — classical Forth semantics. For
embeddings that take untrusted Forth input, build with
`-DFF_SAFE_MEM=ON`:

```sh
cmake -B build -DFF_SAFE_MEM=ON
```

Every address-consuming primitive then validates its pointer against
the engine's tracked regions (any word's heap, the data and return
stacks, the pad ring) and raises `FF_ERR_BAD_PTR` cleanly on a miss.
See [doc/md/20-design.md](doc/md/20-design.md) for the threat model
and what is — and isn't — covered.


## Repository layout

```
src/                    Engine sources. One subsystem per .c file.
src/words/              Built-in word category files.
                          ff_words_<cat>.c        — registration tables
                          ff_words_<cat>_p.h      — opcode case bodies
                                                    (#include'd inside the dispatch switch)
src/3rdparty/           Vendored: fort (table rendering), md4c (markdown),
                        utf8 (UTF-8 helpers).
examples/ffsh/          Reference Forth shell. Standalone CMake project.
test/cases/             Regression-test source pairs (.ff + expected .out).
test/bench/             Benchmark sources + run.sh harness.
doc/md/                 Reference-manual chapters (markdown).
doc/tex/                LaTeX templates used by the PDF build.
debian/                 Source for the libff1 / libff-dev / libff-doc packages.
cmake/                  Build helpers (private) and the consumer-facing
                        ff-config.cmake.in template.
```


## Status

*ff* is at version 1.0.0. The engine is feature-complete for the
ANS-Forth subset it implements; the public API (`ff.h`,
`ff_platform.h`, `ff_error.h`) is considered stable. The internal
`_p.h` headers are installed but flagged as advanced internals — their
struct layouts may change between releases.


## License

[MIT](LICENSE) — copyright 2026 Mikhail Borisov.
