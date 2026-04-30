/**
 * @file ff_config_p.h
 * @brief Compile-time sizing constants for engine buffers and stacks.
 *
 * Adjust these to trade memory footprint against capacity. Values are
 * baked in at compile time — there is no runtime resize for stacks
 * (the heap, by contrast, grows on demand).
 */

#pragma once


/** @brief Depth of the data and return stacks (in cells). */
#define FF_STACK_SIZE       512

/** @brief Initial heap capacity per word, in cells. Doubles on demand. */
#define FF_INIT_HEAP_SIZE   64

/** @brief Depth of the back-trace stack used for error reporting. */
#define FF_BT_STACK_SIZE    256

/**
 * @brief Initial capacity of the transient-string bump arena, in bytes.
 *
 * Strings pushed by interpret-time string literals (`"foo"`) are
 * appended here. The arena grows on demand and is reset by
 * @ref ff_abort. Pointers handed out remain stable for the engine's
 * lifetime (or until the next abort), eliminating the "silent ring
 * recycle" footgun where two long-lived stack pointers ended up
 * aliasing the same slot after >128 string pushes.
 */
#define FF_PAD_INIT_SIZE    (32 * 1024)

/** @brief Maximum length of a single token, in bytes. */
#define FF_TOKEN_SIZE       256

/** @brief Bytes reserved for the most recent error message. */
#define FF_ERROR_MSG_SIZE   512

/** @brief Line buffer size when reading source via ff_load(). */
#define FF_LOAD_LINE_SIZE   4096

/**
 * @def FF_SAFE_MEM
 * @brief Compile-time switch: validate every address before fetch /
 *        store / execute.
 *
 * When defined (`-DFF_SAFE_MEM=1` or CMake `-DFF_SAFE_MEM=ON`), the
 * `@`, `!`, `+!`, `c@`, `c!`, `s@`, `s+`, `strlen`, `strcmp`, and
 * `execute` primitives consult @ref ff_addr_valid (or @ref
 * ff_word_valid for `execute`) on every invocation. An out-of-range
 * pointer raises FF_ERR_BAD_PTR via @ref ff_tracef and unwinds the
 * interpreter cleanly instead of segfaulting.
 *
 * When undefined (the default), the checks compile away to nothing —
 * zero runtime cost. Enable for embeddings that take untrusted Forth
 * input. See doc/md/20-design.md `## Memory safety` for the threat
 * model and what's still vulnerable when the flag is on.
 */
#if !defined(FF_SAFE_MEM)
#  define FF_SAFE_MEM 0
#endif

/**
 * @def FF_R_TRUSTED
 * @brief Compile-time switch: drop redundant return-stack underflow
 *        checks inside opcodes that the compiler emits in matched
 *        pairs.
 *
 * `XLOOP`/`PXLOOP`/`LOOP_I`/`LOOP_J`/`R_FETCH`/`FROM_R`/`EXIT` and
 * friends all run *only* in bytecode the engine itself emitted, in
 * positions where a preceding `XDO`/`>R` provably leaves the right
 * number of items on the return stack. Their @c _FF_RSL(n) check
 * therefore guards an impossible failure — engine bug, not a
 * user-code bug. When this flag is on, those bytecode-internal
 * checks compile away, saving ~5 % on loop-heavy code without
 * weakening any embedder-facing macro.
 *
 * Off by default. The `FF_RSL` / `FF_RSO` macros that custom native
 * words call (via `<ff_p.h>`) are not affected — they still run.
 */
#if !defined(FF_R_TRUSTED)
#  define FF_R_TRUSTED 0
#endif

/**
 * @def FF_VT_COLORS
 * @brief Compile-time switch: render Markdown output (man pages,
 *        diagnostics) with ANSI escape codes for colour and
 *        emphasis instead of plain UTF-8 text.
 *
 * The two render functions in @ref ff_md.h are always available
 * regardless; this flag picks which one engine-internal callers
 * (`ff_print_manual` and similar) reach for by default. Define
 * (set to 1) for desktop / interactive embeddings; leave undefined
 * for log files, network output, or terminals that don't speak
 * ANSI.
 */
#if !defined(FF_VT_COLORS)
#  define FF_VT_COLORS 0
#endif
