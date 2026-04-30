# Changelog

All notable changes to this project are documented here. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project follows [Semantic Versioning](https://semver.org/).


## [Unreleased]

### Added

- Markdown renderer for terminal output. Two snprintf-shaped
  entry points in `<ff_md.h>`:
  - `ff_md_snprintf` — plain UTF-8 (no ANSI codes).
  - `ff_md_vt_snprintf` — ANSI-styled (bold/italic/cyan/underline)
    for headings, emphasis, code, links, strikethrough.
  Both share the same vendored md4c parser and accept a `width`
  parameter for word-wrap. Markdown tables are rendered through
  the vendored fort library with rounded-corner box borders,
  automatic column widths, and bold headers. The new
  `FF_VT_COLORS` compile-time flag in `ff_config_p.h` picks which
  one `ff_print_manual` calls by default; both are always
  available regardless. `man <word>` output is now
  markdown-rendered.
- Performance pass that lifted *ff* above `gforth-itc` and the
  default `gforth` on the arithmetic and memory-traffic
  benchmarks (b2 sum: 290 → 120 ms; b4 var r/m/w: 680 → 250 ms).
  Concretely:
  - Two-op peephole superinstructions: `i + loop` →
    `FF_OP_I_ADD_LOOP`; `<var> @`/`!`/`+!` →
    `FF_OP_VAR_FETCH`/`VAR_STORE`/`VAR_PLUS_STORE`; `swap drop`
    → `FF_OP_NIP`; `swap over` → `FF_OP_TUCK`; `over +` →
    `FF_OP_OVER_PLUS`; `r@ +` → `FF_OP_R_PLUS`.
  - `nip` and `tuck` are also exposed as standalone Forth
    primitives.
  - `ff_heap_inhibit_peephole(h)` plumbed through every
    control-flow immediate (`THEN`, `BEGIN`, `ELSE`, `REPEAT`,
    `LOOP`, `+LOOP`) so a fold can never cross a branch
    target.
  - `__builtin_expect(..., 0)` hints on every validator
    (`_FF_SL`/`_FF_SO`/`_FF_RSL`/`_FF_RSO`/`_FF_COMPILING`/
    `_FF_CHECK_ADDR`/`_FF_CHECK_XT`/`abort_requested`) so the
    rare error path moves to a cold section.
  - `FF_R_TRUSTED` build flag (default OFF) elides the
    bytecode-internal `_FF_RSL` / `_FF_RSO` checks inside
    matched-pair opcodes (XLOOP, XDO, NEST, EXIT, …). Custom
    native words still get full validation.
  - `FF_LTO` build flag wires
    `CMAKE_INTERPROCEDURAL_OPTIMIZATION` for cross-TU inlining.
  - `FF_PGO=GENERATE`/`USE` build flags for profile-guided
    optimisation. Two-pass build with explicit
    `FF_PGO_DATA=path/to/merged.profdata` for the second pass.
- Watchdog API to bound execution time of untrusted Forth code.
  Two complementary mechanisms sharing one `FF_ERR_ABORTED` unwind:
  - Polling callback (`ff_platform_t::watchdog` +
    `watchdog_interval`) — invoked at every back-branch and word
    call; deterministic, no signals or threads required.
  - Async kill flag (`ff_request_abort(ff)`) — safe from a signal
    handler or another thread; picked up at the same dispatch
    sites as the polling callback.
- `defer` / `is` (ANS Forth deferred-word facility).
- `FF_SAFE_MEM` compile-time flag turning every address-consuming
  primitive (`@`, `!`, `+!`, `c@`, `c!`, `s!`, `s+`, `strlen`,
  `strcmp`, `execute`, `evaluate`, `load`) into a bounds-checked
  operation against the engine's tracked regions. Off by default.
  `ff_addr_valid()` and `ff_word_valid()` are public helpers usable
  regardless of build mode.
- `pkg-config` support (`ff.pc`) installed alongside the CMake
  package config.
- `FF_BUILD_EXAMPLES` CMake option that builds `ffsh` as part of the
  main build (instead of requiring a separate `find_package(ff)`
  step against an installed library).
- Decompiler (`see`) now reconstructs control-flow constructs
  (`if`/`else`/`then`, `begin`/`while`/`repeat`, `do`/`loop`, …)
  with proper indentation. Words built via `constant`, `variable`,
  `create`, `array`, `defer` decompile to the form that would have
  created them.
- `man`'s table now has a horizontal rule between the data row and
  the synopsis.
- Reference benchmarks under `test/bench/` comparing *ff* against
  `gforth-itc`, `gforth`, `gforth-fast` on five workloads.
- Memory-safety regression test (`test/cases/008_memsafe.ff`) and
  defer/is regression test (`test/cases/009_defer.ff`).
- Windows build scripts (`build.cmd`, `clean.cmd`).
- MIT licence file at the repository root.

### Changed

- Source layout reorganised: each subsystem in one `.c`, every
  built-in word category split into a registration `.c` plus a
  dispatch-include `_p.h`.
- Inner interpreter switched to **computed-goto threaded dispatch**
  on GCC / Clang (`goto *dt[*ip++]`). Each handler ends with a
  direct indirect branch into a static per-opcode jump table; the
  CPU's indirect-branch predictor can specialize per call-site. MSVC
  retains the `switch (*ip++)` loop via `#ifdef _MSC_VER`. Both
  implementations share the same prologue (`ff_exec_setup_p.h`),
  the same per-category handler headers (`ff_words_*_p.h`), and the
  same epilogue (`ff_exec_teardown_p.h`); only `_FF_CASE(op)` and
  `_FF_NEXT()` differ. Benchmarks updated:
  - b1 empty loop: 310 → 210 ms (−32 %)
  - b5 nested loops: 310 → 210 ms (−32 %)
  - b3 fib(36): 760 → 740 ms (−3 %)
  - b2 sum / b4 var: within measurement noise (peephole-bound).
  *ff* now beats `gforth-itc` and `gforth` on all five benchmarks
  and matches `gforth-fast` on the two pure-dispatch workloads (b1,
  b5).
- Top-of-stack is held in a register-cached local for the duration
  of `ff_exec`.
- Per-word heaps replace the classical contiguous Forth heap. Each
  word owns its body and can `realloc` independently.
- Strings are NUL-terminated C strings rather than counted strings.
- CMake plumbing rewritten around `find_package(ff)` /
  `ff::ff` / `configure_package_config_file`. Build helpers in
  `cmake/ff-helpers.cmake` are private (not installed); the
  consumer-facing config is generated from `cmake/ff-config.cmake.in`.
- Install layout follows `GNUInstallDirs`. `make install` puts
  headers in `<prefix>/include/ff/`, libraries in
  `<prefix>/lib/`, the CMake package config in
  `<prefix>/lib/cmake/ff/`, and HTML+PDF docs in
  `<prefix>/share/doc/ff/`.
- Debian packaging split into three binaries: `libff1` (runtime),
  `libff-dev` (headers + static archive + CMake/pkg-config),
  `libff-doc` (HTML + PDF reference manual).
- `ffsh` writes its history to `$XDG_DATA_HOME/ff/history.ff`
  (`%APPDATA%\ff\history.ff` on Windows) instead of `./history.ff`.
  Override via `$FFSH_HISTORY`.
- Removed spurious `libstdc++6` runtime dependency (the codebase
  is pure C).

### Documentation

- Reference manual chapters in `doc/md/`:
  - `20-design.md` — architecture, opcode set, performance
    optimisations, dictionary, memory-safety mode, lineage from
    Atlast.
  - `30-codestyle.md` — C17 conventions used inside the engine.
  - `40-extending.md` — adding native C words, including the
    `FF_CHECK_ADDR` / `FF_CHECK_XT` validators.
  - `50-benchmarks.md` — comparison against the three gforth
    engines.
