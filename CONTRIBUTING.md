# Contributing to ff

Thanks for taking the time to look. *ff* is a small project run by a
small team, so this document is short and practical rather than
exhaustive.


## Reporting bugs

A useful bug report includes:

- The version of *ff* (`git rev-parse HEAD` or the package version).
- Your build configuration: compiler + version, OS, the relevant
  `cmake` flags (especially `-DFF_SAFE_MEM=...` and
  `-DCMAKE_BUILD_TYPE=...`).
- A minimal Forth source file that reproduces the issue, plus the
  exact command line that runs it.
- The actual output and what you expected instead.

Open issues at https://github.com/mikhailborisov/ff/issues. Memory-
safety bugs (assertion failures, segfaults from valid Forth code,
out-of-bounds reads/writes) are higher priority than language-feature
gaps and will be looked at first.


## Submitting changes

The workflow is the standard fork → branch → pull-request loop.
Before opening a PR:

1. **Make sure the test suite passes in both build modes.**

   ```sh
   cmake -B build/default -DFF_BUILD_TESTS=ON
   cmake --build build/default && ctest --test-dir build/default

   cmake -B build/safe -DFF_BUILD_TESTS=ON -DFF_SAFE_MEM=ON
   cmake --build build/safe && ctest --test-dir build/safe
   ```

2. **Add a regression test** for any user-visible change. The test
   harness lives under [`test/`](test/) — drop a `NNN_topic.ff`
   alongside its `.out` expected output and re-run cmake to pick the
   pair up.

3. **Update the relevant chapter under [`doc/md/`](doc/md/)** if the
   change touches:
   - the engine struct, dictionary, or heap (`20-design.md`);
   - the public API or the `_p.h` private surface (`40-extending.md`);
   - the opcode set or compiler peephole pass (`20-design.md`).

4. **Add an entry to [`CHANGELOG.md`](CHANGELOG.md)** under
   `[Unreleased]`.

5. **Match the existing C style.** [`doc/md/30-codestyle.md`](doc/md/30-codestyle.md)
   spells out the conventions; the short version is K&R-style braces,
   ISO C17, no trailing whitespace, four-space indent. The codebase
   compiles clean under `clang -Wall -Wextra -Werror`; please keep
   it that way.


## Local build

The minimum tools you need are CMake (≥ 3.10), a C17 compiler, and
`make` or `ninja`. Optional: `clang` (the canonical compiler used by
CI), `doxygen` and a TeX distribution if you're rebuilding the
reference manual, and `gforth` plus `gforth-fast` if you're running
the benchmark suite under [`test/bench/`](test/bench/).

A typical development cycle:

```sh
cmake -B build -DFF_BUILD_TESTS=ON -DFF_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# Quick interactive test
./build/ffsh
```


## Architectural ground rules

Before refactoring large pieces, please open an issue first. A few
choices that look like accidents but aren't:

- **No global state.** Every interpreter instance is one `ff_t *`;
  multiple instances must coexist freely.
- **All output via `ff_platform_t` callbacks.** No direct `printf`,
  `fputs`, or `fprintf` from inside the engine.
- **Per-word heaps, not a global heap.** This is what makes
  `forget` and incremental word growth simple, and it's load-bearing
  for the `does>` / `create` / `defer` runtimes — see the design doc.
- **Switch dispatch with `#include`d case bodies, not
  computed-goto.** This is the difference between building under MSVC
  and not.
- **C strings, not counted strings.** A pointer alone identifies a
  string; getting the length is an O(n) `strlen`.

If you find one of those genuinely needs to change, that's worth a
discussion before code lands.


## Licence

By contributing, you agree your changes are released under the same
[MIT licence](LICENSE) as the rest of the project.
