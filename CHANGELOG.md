# Changelog

All notable changes to this project are documented here. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project follows [Semantic Versioning](https://semver.org/).


## [Unreleased]

### Added

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
- Inner interpreter now uses a `switch (*ip++)` whose case bodies
  are `#include`d from the per-category `_p.h` headers. Top-of-stack
  is held in a register-cached local for the duration of `ff_exec`.
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
