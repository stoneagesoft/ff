# Changelog

All notable changes to this project are documented here. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project follows [Semantic Versioning](https://semver.org/).


## [Unreleased]

### Added

- Public embedding API so hosts can register C words and marshal data
  using only `<ff.h>` — no internal headers:
  - `ff_register(ff_t*, const ff_native_word_t*)` with the
    `FF_NATIVE` / `FF_NATIVE_I` / `FF_NATIVE_END` table macros.
  - Data-stack marshaling: `ff_depth`, `ff_push_int` / `ff_pop_int`,
    `ff_push_real` / `ff_pop_real`.
  - `ff_version()` (and `<ff.h>` now pulls in the `FF_VERSION*` macros),
    plus `ff_warmup()` to initialise the shared built-in table up front
    for multi-threaded hosts.
  - `FF_ERR_CODE()` / `FF_ERR_SEV()` for splitting a packed `ff_error_t`.
- Build now uses `-fwrapv`: signed overflow wraps two's-complement, as
  Forth arithmetic expects, and the optimizer can't assume it away.
- `recurse`, the standard spelling of a definition's call to itself
  (calling it by name keeps working).
- **Limits for untrusted code**, set per engine in `ff_platform_t`:
  - `mem_limit` caps the memory Forth code can make the engine hold —
    word storage, words, and the transient string arena. Beyond it an
    allocation raises -8 (`FF_ERR_HEAP_OVER`).
  - `deny` withholds `system` (`FF_CAP_SYSTEM`), the file words and
    `stdin` / `stdout` / `stderr` (`FF_CAP_FILES`) and `load`
    (`FF_CAP_LOAD`); a withheld word raises -21 when it runs.
  - `open_file` and `run_command` replace `fopen()` and `system()`, so
    the host can confine or virtualise file access and vet commands.
    `ff_load()` opens files through the hook too.
- CMake options `FF_WITH_SYSTEM` and `FF_WITH_FILES` (both ON) leave the
  `system` word, or the file words and `load`, out of the build.
- `FF_CHECK_WRITE` / `ff_addr_writable()` and `FF_CHECK_STR` /
  `ff_str_valid()` for native words that write through, or read a
  string from, a pointer off the stack.
- `ff_find()` looks a word up by name for `ff_exec()`, so a host no
  longer needs the internal `ff_dict_lookup()`; `ff_throw_code()` gives
  the THROW code that ended the last call, which is how the host tells
  a program's own `n throw` codes apart (`ff_errno()` reports them all
  as `FF_ERR_APPLICATION`).
- The public headers declare the API `extern "C"` and are tested from a
  C++ host.
- For native words: `ff_context()` returns the host's
  `ff_platform_t::context`; `ff_push_str()` / `ff_pop_str()` pass
  strings (validated under `FF_SAFE_MEM`); `ff_throwf()` throws a THROW
  code of the program's own, with a message.
- `ff_release_strings()` frees the transient string arena between calls,
  for long-running hosts: it otherwise only grows, until the memory
  limit makes every allocation fail.

### Fixed

- **Crashes on ordinary input:** `INT_MIN / -1` (and `mod`/`/mod`) no
  longer raise `SIGFPE`; a negative `pick`/`roll` index no longer reads
  past the stack top; `allot` with a negative count no longer truncates
  into a huge allocation. All now raise a clean error.
- **String-pointer stability:** the transient string arena is a list of
  non-moving slabs instead of a single `realloc`'d buffer, so `char*`
  values handed out by string literals and `parse-word` / `parse` stay
  valid as more strings are interned — the documented contract was
  previously broken by the growth `realloc`.
- **Native words:** a leaf native word left `ff->ip` stale (dispatch ran
  garbage) and an early `goto done` used an uninitialised TOS register
  (clobbered the pushed result). Both are fixed — the fn-based native
  path is now exercised by the public API.
- **Error codes are usable again:** `ff_eval` / `ff_exec` / `ff_errno`
  return the bare `FF_ERR_*` code, so `ff_errno(ff) == FF_ERR_DIV_ZERO`
  works (they previously carried the severity bit and never matched).
- **State-machine leaks:** a wrong-kind token after `:` / `'` / `[']` /
  `."` / `postpone` now errors instead of leaking the pending flag onto
  a later token and silently miscompiling; `abort` inside a scope no
  longer leaves stale scope records that could hijack later
  interpretation.
- **Diagnostics instead of silence:** an unterminated string literal, a
  token longer than `FF_TOKEN_SIZE`, and an over-range integer literal
  are now reported rather than silently dropped, truncated, or clamped.
- Size-overflow guards on the heap/dict/arena growth paths;
  `ff_heap_compile_str` uses `size_t` (was a truncating `int`);
  `ff_new` returns `NULL` on allocation failure; `ff_heap_trim` bumps
  the safe-mem interval counter; `ff_dict_rename` tolerates a failed
  `strdup`; the watchdog now ticks through `does>`-built words.
- **Resuming after a nested run:** `evaluate` crashed on every use, and a
  call to a deferred word silently ended the word that made it. Both
  reloaded the caller's instruction pointer from `ff->ip`, which the
  nested run always leaves NULL.
- **Stack overflow:** literals pushed by the evaluator, the return frame
  `ff_exec` pushes for `execute` / deferred words / `catch`, and the
  result `catch` pushes are now bounds-checked. Previously a long line of
  literals overwrote the stack's own bookkeeping, and recursion through
  `execute` or a deferred word ran off the end of the return stack. So
  did any deep recursion under `FF_R_TRUSTED`, which also removed the
  NEST / DO overflow checks; it now removes only underflow checks.
- **State left behind by errors:** an error exit now cuts the return
  stack and the `{ }` scope barrier back to where that `ff_exec` started.
  Each runtime error used to leak return-stack cells until the array
  overflowed, and an error (or `exit`) inside a scope left its barrier
  raised, hiding the caller's cells.
- **No current definition:** `immediate`, `here`, `,`, `c,`, `allot`,
  `c=`, `]` and `does>` raise `FF_ERR_NOT_IN_DEF` on a fresh engine
  instead of dereferencing NULL; `does>` also refuses to run outside a
  defining word, where it recorded a pointer into a dead C stack frame.
- **`see`** steps over every instruction's operands. It advanced one cell
  past opcodes without a special case, so inline strings and the word
  pointer of a constant / array / `create` / `defer` / `does>` reference
  were decoded as opcodes — the wrong name, garbage, or a crash. String
  and real literals now print in a form that reads back (escaped; `1.0`
  rather than `1`).
- **`+loop` with a negative step** terminates: it now ends when the index
  crosses the limit in either direction (ANS 6.1.0140). Counting down
  used to loop forever.
- **Watchdog:** a nested `ff_eval` (`evaluate`, `load`) no longer resets
  the opcode count or clears a pending `ff_request_abort()`, so a loop
  around `evaluate` can be stopped. The abort is reported as
  `FF_ERR_ABORTED`, as documented, instead of `FF_ERR_BROKEN`.
- **Error location:** `ff_err_pos()` was always 0 and `ff_load()` lines
  were numbered from 0; both now locate the offending token.
- **`ff_load`:** lines longer than 4 KiB were split mid-token; a missing
  file returned `FF_SEV_ERROR | FF_ERR_FILE_IO` rather than the bare
  code; a runaway `(` comment kept swallowing the caller's next input;
  and a nested `load` reset its caller's line count.
- **String escapes:** a malformed `\x` / `\u` / `\U` escape consumed the
  character after it (`"\x"` swallowed its own closing quote), and an
  8-digit `\U` overflowed `int`. Such escapes are now rejected.
- `0xFFFFFFFFFFFFFFFF` (any hex literal past the signed range) pushed a
  hex-float double; hex literals are now cell bit patterns.
- `clear` and `depth` inside a `{ }` scope work on the cells above the
  barrier only; `clear` there used to crash the closing `}`.
- `<ff_p.h>` compiles under `-Wall -Werror`: it declared four `static`
  functions it never defined.
- Removed the leftover `(nest)` native word, which crashed when run.
- **Errors stop the code that raised them.** An error inside a word run
  by `execute`, a deferred word, `catch` or a native word used to let its
  caller carry on running, and only the outer interpreter noticed; code
  after a `throw` inside an `execute`d word ran too. Errors now unwind
  like `throw`.
- An uncaught `throw` left a flag set that made the next, unrelated
  `catch` report the old code instead of 0; it also returned
  `FF_ERR_BROKEN` with no message.
- `quit` inside `execute` or `catch` emptied the return stack under the
  running words, which then carried on.
- `abort` inside a nested run reset the stacks under the words still
  running above it.
- Recursion through `execute` or a deferred word no longer uses the C
  stack.
- Running an internal word such as `branch`, `(xdo)` or `(strlit)`
  directly no longer jumps into memory past its operand; it does nothing.
- **A definition that fails is discarded.** An error before `;` — an
  undefined word, a stray token where the name should be, a failing word
  run between `[` and `]` — left the word in the dictionary half-compiled
  and without an `EXIT`, so calling it ran off the end of its body. The
  word is now removed, as is one that `ff_abort()` or an uncaught `abort`
  interrupts, and `create`, `variable`, … no longer leave a nameless
  word behind when the name doesn't come. An error that an `evaluate`
  inside `[ ]` catches leaves the definition open.
- **Control-structure mismatches are compile errors** (-22). The
  compiler kept `if` / `begin` / `do` bookkeeping as bare heap offsets
  on the data stack: `[ 100000 ] then` wrote far outside the word,
  `begin then` patched a cell that was never a branch, an `if` still open
  at `;` left a branch past the end of the word, and a structure could
  straddle a `{ }` scope.
- **`exit` inside a `do` loop** returned through the loop's parameters
  as if they were a return frame, and `leave` outside a loop jumped
  through a return frame; both crashed. `exit` now drops the loop
  parameters first, and `leave` outside a loop is a compile error.
  `exit` and `leave` inside a `{ }` scope close it first, checking its
  outputs, where the barrier used to stay raised in the caller. `does>`
  inside a loop or a scope is rejected.
- A word calling itself as its first token (`: f f … ;`) compiled a call
  through a garbage pointer. That also crashed a redefinition such as
  `: foo foo 1 + ;`, which now recurses into the new `foo`, as a call by
  name does.
- **Compile target:** code went into whichever word was newest, so a
  `create` or `variable` between `[` and `]` took over the rest of the
  definition, `:` inside a definition started a second one on top of the
  first, and `forget` could free the definition being compiled. Code now
  goes into the word `:` made; `:` there raises -29 and `forget` -15.
- `abort"` in an immediate word compiled itself into the word being
  defined and then ran its string as code; `.(` in a definition compiled
  its string's bytes as instructions; and `abort"` at the prompt raised
  with garbage as its message.
- `compile` copied a single cell of compiled code — half of the two-cell
  call of a colon definition — and the cell after it then ran as an
  opcode.
- **Sizes and allocation failures:** `-1 array`, `-100 string` and a
  huge `allot` crashed the host. Negative sizes (and `0 allot`) now
  raise -24, and an allocation that fails — over the new memory limit,
  too large to represent, or refused by `malloc` — raises -8 or -59
  instead of writing through a NULL or undersized buffer. That covers
  heap growth, new words and the string arena.
- **Memory is given back:** `forget`, and a definition that fails,
  return their words' storage to the arena, so defining and forgetting
  in a loop holds steady; it used to grow without bound. `ff_free()`
  closes files the program left open, and frees the input names of a
  `{` scope in a definition the input left unfinished (both leaked).
- **`FF_SAFE_MEM` gaps:** `?`, `type`, `dump`, `find`, `>name`, `>body`
  and the file words used a pointer, string, xt or stream from the stack
  unchecked, and the string words and `evaluate` checked one byte of a
  string and then read past its end. All are checked now, file streams
  against the ones the program opened.
- **Bytecode could be forged under `FF_SAFE_MEM`:** `!` into a colon
  definition, `,` inside `[ ]`, or `s!` over a compiled string literal
  could plant a word pointer for the interpreter to follow. Bytecode and
  native fn pointers are now read-only to Forth code.
- A word that ran `create` (or `variable` …) twice made a second word
  before the first got its name; the first could never be named and
  piled up. Each now reads its own name: `: two create create ;  two a b`
  makes `a` and `b`.
- `forget` inside `evaluate` could remove the word running it, or that
  word's caller, and free its code under it. It now raises -15 when a
  word it would remove is running.
- A word name ending in a cut-off UTF-8 sequence (`ab\xDA`) was read
  past its end whenever it was looked up or defined again, and never
  matched. Names are now compared the way they are hashed: ASCII letters
  regardless of case, every other byte exactly. (Non-ASCII letters of
  another case matched only when both spellings happened to hash alike.)
- `see`, `man`, `dump-word` and `parse-word` read through a NULL pointer
  when the host ran them with `ff_exec()` outside any evaluation. The
  first three now raise `FF_ERR_MISSING`; `parse-word` returns an empty
  string, as at the end of a line.
- `s!` and `s+` with overlapping strings (`s dup s+`) no longer use
  `memcpy` on overlapping ranges.
- The fuzzing harness ran with `system` and the file words live, so a
  generated input could run commands on the fuzzing machine. It now
  denies them, caps memory and time, and evaluates line by line.
- `pick` and `roll` took their index as an `int`: a huge index read far
  outside the stack (a crash, even under `FF_SAFE_MEM`), one past 2^32
  wrapped to a small index, and a `roll` reaching too deep wrote its
  index over the item below. The index is now checked as a whole cell
  before anything moves.
- A native word that ran Forth itself — `ff_eval()`, or `ff_exec()` of
  a word — made the word that called it stop right after it, silently
  skipping the rest of its body. `ff_exec()` now hands `ff->ip` back as
  it found it.
- A `{ }` scope that left the return stack unbalanced raised its error
  but kept its barrier over the caller's cells: afterwards `depth` read
  0 and `drop` failed on cells that were still there.
- Under `FF_SAFE_MEM`, a pointer into the middle of a built-in word
  passed for an xt, so `' dup 24 + execute` crashed; `catch`, deferred
  words, `>name` and `>body` had the same gap.
- `parse` with a delimiter of 0 (or 256, …) consumed the input's
  terminator, and the next read ran past the end of the input. It now
  takes the rest of the input.
- A `(` comment still open at the end of an `evaluate` string ran on
  into the caller's input and the lines after it, and a `\` ending a line
  of a multi-line input commented out the next line too.
- `fix` of NaN, an infinity, or a real whose integer part doesn't fit a
  cell was undefined behaviour in C; it now raises -24.
- `@`, `!`, `+!` and `?` accessed a cell at an unaligned address —
  easy to make with `c,` — through a misaligned pointer: undefined
  behaviour, and a fault on strict-alignment CPUs. They copy the cell
  now.
- The manual gave `state` as `( -- addr )`, so the `state @` it implied
  crashed. `state` pushes the flag itself, and the manual says so.
- The test driver ignored how a whole-file test case ended, so a test
  of an error path passed even if the error never happened. The error
  is now part of the expected output, as in line-by-line cases.
- **Host API:**
  - A C++ host couldn't link: the public headers had no `extern "C"`.
  - `ff_depth()` and the pops ignored a `{ }` scope's barrier, so a
    native word could eat its caller's cells; and a native's failed
    pop went unnoticed, its caller running on. Inside a running word a
    failed push or pop is now the word's stack error.
  - A host calling `ff_exec()` in a loop had the watchdog's opcode
    budget run on across calls, until every call was aborted, and a
    stale `ff_request_abort()` ended the next call. `ff_exec()` from the
    host now starts afresh, as `ff_eval()` does.
  - An error the host raised itself (`ff_tracef()` with nothing
    running) made every later call return at once, running nothing.
  - `ff_errno()` kept reporting an old error after a later call
    succeeded, and errors that `catch` handled.
  - `ff_exec(NULL)` crashed; it is an `FF_ERR_BAD_PTR` error.
  - A native word popping through a scope's barrier with the internal
    stack API made the scope's exit slide a negative count of cells;
    it is a stack-underflow error.
- **Scaling:**
  - Under `FF_SAFE_MEM`, the index of word heaps was rebuilt and
    re-sorted after every change to the dictionary, so defining words
    and checking addresses in turn took quadratic time — untrusted code
    could burn CPU without tripping the watchdog or the memory limit
    (32,000 variables defined and stored: 17 s, now 14 ms). The arena
    now keeps the index sorted as heaps change, and xts are checked by
    binary search instead of a scan of every word.
  - The hash table of user words never grew: with 100,000 of them, a
    lookup of a built-in walked hundreds of entries first (60× slower).
    It doubles as needed now.
  - `forget` and every failed definition searched the arena's slabs for
    each remaining word: with 20,000 larger definitions, about 13 ms
    each; now 0.1 ms.
  - A heap that grew was always copied to a fresh region, abandoning
    the old one: roughly half the memory a program was charged went to
    dead copies. The arena's newest allocation — the definition being
    compiled, a data word being filled — now grows where it lies (100
    definitions of 600 cells: 403 KB instead of 785 KB), and under
    `FF_SAFE_MEM` an address into it stays valid as it grows. A word's
    charge against `mem_limit` now includes its index entries.
- Reals depended on the host's C locale: under one with a decimal comma
  (which GUI toolkits set for you), `1.5` was an undefined word, `2,5` a
  real, and `f.`, `.s` and `see` printed a comma. Reals now read and
  print with `.` regardless. A real too small to be represented normally
  (`1e-310`) was rejected as if it had overflowed; it reads now.
- A deferred word whose action was forgotten, or was a definition that
  failed, went on calling the freed word. Removing words now resets such
  a deferred word to having no action.
- `see` printed an xt compiled by `[']` as a raw address; it prints
  `['] name`.
- `ff_new()` crashed if memory ran out while it built the built-in table
  or the dictionary; it returns NULL, and a later call tries again.
- `ff_register()` accepted names no program could call — empty, with a
  space, a number — and shadowed built-ins without the warning `:`
  gives; a script's `forget` could remove the host's words. It returns
  `FF_ERR_MALFORMED` for such a name now, warns of shadowing, and
  `forget` leaves registered words in place.

### Changed

- **One exception mechanism.** Every error is an exception carrying an
  ANS THROW code (-4 stack underflow, -10 division by zero, -13 undefined
  word, …), so `catch` catches errors as well as `throw`s. `abort` and
  `abort"` are `-1 throw` / `-2 throw`: catchable, and an uncaught one
  resets the engine and returns `FF_ERR_ABORTED`. `abort"` now makes its
  text the error message instead of printing it. `quit` (-56) and the
  watchdog / `ff_request_abort()` abort (-28) can't be caught.
- `quit` discards the rest of the input line and returns `FF_OK`;
  previously the line ran on.
- `evaluate` and `load` push the THROW code that stopped them (0 on
  success), as `catch` would — for example -13 for an undefined word —
  and the error goes no further. They used to push a positive `FF_ERR_*`
  code and let the error surface again when the outer line finished.
- `ff_exec()` returns false for any exception that escapes the word
  (errors used to return true), and settles it when the host called it
  directly. `ff_abort()` called from inside a running word raises ABORT
  instead of resetting the engine under its callers.
- An uncaught `throw` of a code the engine doesn't define returns
  `FF_ERR_APPLICATION` with the message "Uncaught exception N.".
- `execute` and deferred words run the target inside the dispatch loop,
  through a per-word executable stub (`ff_word_t::stub`), instead of
  calling `ff_exec` recursively.
- `FF_STATE_BROKEN`, `FF_STATE_ABORTED` and `FF_STATE_ERROR` are gone;
  `FF_STATE_THROWN` marks an exception in flight.
- `."`, `abort"` and `.(` only parse their string; separate primitives do
  the run-time work, so what they do no longer depends on STATE or on
  how they are invoked. `.(` prints at once inside a definition too, as
  in ANS, and compiles nothing. `abort"` at the prompt throws -2 at once.
- `compile` is immediate and parses the word it compiles: `compile w` in
  an immediate word compiles a call to `w` when that word runs — like
  `postpone`, but also for an immediate `w`.
- **Parsing words read their input when they run.** `:`, `create`,
  `variable`, `constant`, `defer`, `array`, `string`, `'`, `[']`,
  `[compile]`, `postpone`, `compile`, `forget`, `is`, `."`, `.(` and
  `abort"` read the name or string that follows them themselves, as in
  standard Forth, instead of setting a flag for the interpreter to act
  on at the next token, wherever that came from. The name or string must
  now be on the same line — strictly, in the same `ff_eval()` input,
  which `ffsh` and `ff_load()` pass a line at a time: `:` at the end of
  one raises `FF_ERR_MISSING` rather than taking its name from the next. A parsing word run from
  compiled code reads the input after its caller (`: tick ' ;  tick dup`
  pushes `dup`'s xt). A missing name after `see`, `man` or `dump-word`
  raises `FF_ERR_MISSING` too. The pending flags other than
  `FF_STATE_SIG_PENDING` are gone.
- `is` is immediate and state-smart, as in ANS Forth: in a definition it
  reads the deferred word's name while compiling, and the definition
  sets the action when it runs (`: use-ten ['] ten is hook ;`). Before,
  it looked for the name only when the definition ran.
- `-inf` and `-nan` no longer read as reals (`inf` and `nan` never did):
  a real literal is digits with a fraction and/or an exponent.
- `ff_warmup()` returns false if memory ran out. Its documentation now
  says what holds: with C11 atomics, building the shared built-in table
  on first use is safe from any number of threads; only without them
  (MSVC) must a multi-threaded host call it first.
- Control structures are tracked on a compile-time stack of their own
  instead of the data stack. `while` follows ANS: a loop may have several,
  each after the first closed by a `then` after the `repeat`
  (`begin … while … while … repeat … then`), and `see` prints them back.
- New THROW codes -22 (control structure mismatch) and -29 (compiler
  nesting), reported to the host as `FF_ERR_MALFORMED`. `}` without `{`
  and `;` with a scope still open now raise -22 as well.
- `]` needs an open definition (`FF_ERR_NOT_IN_DEF` otherwise) instead
  of any word to compile into, and scope input names are recognised only
  while compiling.
- `allot` with a count below 1 raises -24 (invalid numeric argument)
  rather than a stack-underflow error.
- Under `FF_SAFE_MEM`, `>name` and `strerror` return a copy in the
  string arena (so `type` accepts it), `fclose` accepts only streams the
  program opened, and `,` / `c,` / `allot` only extend data words.
- A program can have at most `FF_OPEN_FILES_MAX` (32) streams open.
- `does>` applies only to a word made by `create`, as in ANS; run on a
  colon definition it raises `FF_ERR_NOT_IN_DEF` instead of turning the
  definition's bytecode into data.
- A failed allocation no longer crashes on the spot (the old "fail-fast"
  policy): the operation that needed it does nothing and the engine
  raises the error at its next check.
- Opcodes are listed once, as `FF_OPCODES` in `ff_opcode_p.h`; the enum
  and the operand-layout table are generated from it, and `see` names
  an opcode after its built-in word. `ff_opcode_layout()` replaces
  `ff_opcode_meta()`.
- An external native word keeps its C function in `ff_word_t::fn`
  (`ff_word_native_fn()` returns it) instead of in a heap allocation of
  64 cells holding the one pointer.
- Removed: the second registration of `ERRNO`, and the `ff_rc` CMake
  hook, whose resource-compiler script isn't in the repository.

- `abort` / `abort"` now discard the rest of the input line and return
  `FF_ERR_ABORTED` (ANS `ABORT` semantics) instead of running on.
- Integer literals are base 10 by default with explicit `0x` hex; C's
  implicit octal is gone (`010` is ten, `009` is nine).
- The inline strings of `."` and `abort"` are correctly tagged
  `FF_OP_LAYOUT_STR` in the opcode metadata, so `see` / `dump-word` no
  longer mis-decode a word's body after an inline string.
- `ff_err_line()` is 1-based for `ff_eval()` input too (the line within
  the evaluated string), and 0 only when there is no source position.
- A malformed string escape is an error (`FF_ERR_MALFORMED`) instead of
  being replaced by `0xFF` / U+FFFD. Hex floats such as `0x1p3` are no
  longer accepted as numbers.
- Test driver: a `\ ff-test: lines [budget=N]` first line evaluates the
  file line by line and records each failure as `[ERROR_NAME]`. New
  `ff_api_test` covers engine state across calls. CI fails if no tests
  are found; the expected-output files were being excluded by
  `.gitignore`, so CI had run no tests at all.

### Added (language)

- Input-stream words `parse-word`, `parse`, and `postpone`, which let
  new notation be defined from inside Forth instead of only in C.
  - `parse-word ( -- c-addr )` returns the next whitespace-delimited
    token as a NUL-terminated C string (not a dictionary lookup, unlike
    `'`). `parse ( char -- c-addr )` returns the text up to and
    including a delimiter character. Both intern into the transient
    string arena via the new `ff_pad_intern` helper (extracted from the
    string-literal path).
  - `postpone` appends a word's compilation semantics to the current
    definition: a direct call for an immediate target, or (via the new
    `FF_OP_POSTPONE_RUNTIME` opcode) deferred compilation for a
    non-immediate one — correct for multi-cell colon-defs, which the
    older `compile` primitive is not. Supersedes `compile` / `[compile]`.
  - Combined with the existing `evaluate`, these are enough to build
    conditional compilation, enum blocks, and similar DSL syntax in
    pure Forth; the regression suite's `015_parse.ff` defines
    `[if]` / `[then]` as a worked example.
- `ffsh` now ships a Forth **prelude** (`examples/ffsh/prelude.ff`),
  embedded into the binary at build time and evaluated once at startup.
  It defines `char` and `[char]` — character-literal words built on
  `parse-word` / `postpone` — as convenience shortcuts. The prelude is
  part of the shell, not the `ff` library: the engine keeps its
  no-built-in-Forth, no-global-state stance, and the prelude demonstrates
  that such conveniences are a library concern rather than an engine one.
- Checked stack scopes: `{ ( a b -- c ) … }`. A scope names a
  definition's inputs and walls off the data stack — code between `{`
  and `}` starts from an empty stack, reads its arguments only by name,
  and is checked at `}` for leaving exactly the declared number of
  outputs. Enforcement is at run time and cheap: the underflow check
  every stack word already runs (`FF_SL`) now measures against a
  movable floor rather than zero, so a stray `drop` inside a scope
  faults instead of consuming the caller's cell. The return stack is
  likewise asserted balanced at `}`, and `catch` restores the barrier
  when a `throw` unwinds out of an open scope.
  - `...` opts out of a check: `( a -- ... )` leaves an unchecked cell
    count; `( ... -- ... )` inherits the enclosing barrier instead of
    installing a new one. `...` on the input side cannot be combined
    with named inputs and forces `...` on the output side.
  - Named inputs compile to indexed reads below the barrier
    (`FF_OP_ARG`), never dictionary entries, so a name shadows any word
    of the same spelling for the body of its scope and needs no cleanup.
  - The signature source is stored with the word (keyed by bytecode
    offset), so `see` renders a scoped word back with its signature and
    resolves each named input to the name it was written as.
  - New opcodes `FF_OP_SCOPE_ENTER` / `FF_OP_SCOPE_EXIT` / `FF_OP_ARG`
    and immediate words `{` / `}`; new state flag `FF_STATE_SIG_PENDING`
    and tokenizer flag `FF_TOK_STATE_SIG` (which suspends `( … )`
    comment handling for the duration of a signature); new error codes
    `FF_ERR_SCOPE_ARITY` / `_OVER` / `_RSTACK` / `_SIG`. Scopes are
    compile-mode only, matching `if` / `do` / `begin`.
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
    bytecode-internal `_FF_RSL` underflow checks inside
    matched-pair opcodes (EXIT, XLOOP, LEAVE, I, J, …); overflow
    checks always stay. Custom native words still get full
    validation.
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

- **ABI break** (public `ff_error.h`): the scope error codes were
  inserted in alphabetical position, so the numeric values of
  `FF_ERR_STACK_OVER` and everything after it shifted. Rebuild
  consumers against the new headers; do not mix a new `libff.so` with
  objects compiled against the old enum.
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
    engines, stock Lua 5.4, and stock CPython 3.
