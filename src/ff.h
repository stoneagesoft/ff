/**
 * @file ff.h
 * @brief Public engine API for the ff Forth interpreter.
 *
 * This header declares the lifecycle, evaluation, and reporting
 * functions that an embedder calls to drive an ff interpreter
 * instance. Internal layouts (struct ff, struct ff_word, …) are
 * deliberately opaque here — pull in `ff_p.h` from inside the library
 * (or from a custom-word source file) to access them.
 */

#pragma once

#include <ff_error.h>

#include <stdarg.h>
#include <stdbool.h>


/**
 * @def FF_PRINTF_FMT(i, j)
 * @brief Compiler hint that the @p i-th printf-style format string is
 *        consumed by the @p j-th variadic argument.
 *
 * Expands to GCC/Clang's `__attribute__((format(printf, i, j)))` so
 * mismatched format-string arguments are caught at compile time. On
 * compilers that don't support the attribute the macro vanishes.
 */
#if defined(__GNUC__) || defined(__clang__)
#define FF_PRINTF_FMT(i, j) __attribute__((format(printf, i, j)))
#else
#define FF_PRINTF_FMT(i, j)
#endif


/** @brief Opaque engine instance — created with ff_new(). */
typedef struct ff ff_t;
/** @brief Embedder-provided platform callbacks (printf / trace). */
typedef struct ff_platform ff_platform_t;
/** @brief Dictionary entry; opaque from this public header. */
typedef struct ff_word ff_word_t;

/**
 * Allocate and initialize a new interpreter instance.
 *
 * Sets up the dictionary (registering every built-in word), the data
 * and return stacks, the back-trace stack, and the tokenizer. The
 * platform callbacks are copied by value so @p p can be a stack
 * temporary at the call site.
 *
 * @param p Platform callbacks (printf, optional trace). Must not be NULL.
 * @return Newly allocated engine, owned by the caller. Release with
 *         ff_free().
 */
ff_t *ff_new(const ff_platform_t *p);

/**
 * Tear down an interpreter created with ff_new() and release all
 * memory it owns. Safe to pass NULL.
 *
 * @param ff Engine instance, or NULL.
 */
void ff_free(ff_t *ff);

/**
 * Tokenize and execute a string of Forth source code.
 *
 * Each token is dispatched: integers/reals/strings push to the data
 * stack (or compile a literal during a colon-def), and word names are
 * looked up and executed (or compiled). On error the engine's error
 * fields are populated and the appropriate code is returned.
 *
 * @param ff  Engine instance.
 * @param src Source text. NULL or empty input returns FF_OK without
 *            touching engine state.
 * @return FF_OK on success, otherwise one of the FF_ERR_* codes
 *         declared in ff_error.h.
 */
ff_error_t ff_eval(ff_t *ff, const char *src);

/**
 * Execute a single dictionary entry directly.
 *
 * Used for the immediate path in ff_eval (when not compiling) and by
 * the EXECUTE word. Synthesizes a tiny `[opcode, …, EXIT]` scratch
 * buffer for opcoded built-ins, falls through to the FF_OP_CALL escape
 * hatch for external natives, and steps the inner interpreter for
 * colon-defs.
 *
 * @param ff Engine instance.
 * @param w  Word to execute. Must not be NULL.
 * @return true on normal completion, false if the BROKEN flag was
 *         raised mid-execution.
 */
bool ff_exec(ff_t *ff, ff_word_t *w);

/**
 * Load a file as if every line were passed to ff_eval() in order.
 *
 * @param ff   Engine instance.
 * @param path File path; NULL or empty is a no-op returning FF_OK.
 * @return FF_OK on success, FF_ERR_FILE_IO if the file cannot be
 *         opened, or whatever code the first failing ff_eval() call
 *         produces. A run-away `(` comment that survives the last line
 *         is reported as FF_ERR_RUN_COMMENT.
 */
ff_error_t ff_load(ff_t *ff, const char *path);

/**
 * Reset the engine's transient state: clears both stacks, drops the
 * current IP, raises the ABORTED flag, and clears the tokenizer's
 * comment state. Word definitions and the dictionary are preserved.
 *
 * @param ff Engine instance.
 */
void ff_abort(ff_t *ff);

/**
 * Asynchronously request the engine to stop running.
 *
 * Sets a `sig_atomic_t` flag that the inner interpreter polls at
 * every back-branch and word call. Once detected, the running
 * `ff_eval` / `ff_exec` unwinds and returns FF_ERR_ABORTED. Safe to
 * call from a signal handler or another thread — it does no I/O,
 * no allocation, and no engine state mutation beyond the flag store.
 *
 * Pairs with the polling watchdog callback in @ref ff_platform_t,
 * which is what most embeddings should reach for first; this
 * function is the escape hatch when the host's "stop now" signal
 * arrives via a path the polling callback can't observe (alarm
 * signal, GUI thread, …).
 *
 * The flag is consumed (cleared) on the next ff_eval entry, so a
 * call between evaluations is silently ignored — abort requests
 * only apply to in-flight execution.
 *
 * @param ff Engine instance.
 */
void ff_request_abort(ff_t *ff);

/**
 * @return The startup banner string (ASCII art logo). The pointer is
 *         owned by the engine and remains valid for the engine's
 *         lifetime.
 * @param ff Engine instance (currently unused, reserved for future
 *           per-instance variants).
 */
const char *ff_banner(const ff_t *ff);

/**
 * @return A short prompt fragment reflecting the current input mode
 *         (interpret, compile, run-on `(` comment). Lives inside the
 *         engine — do not free.
 * @param ff Engine instance.
 */
const char *ff_prompt(const ff_t *ff);

/**
 * Last error code recorded by the engine (FF_OK if none).
 * @param ff Engine instance.
 */
ff_error_t ff_errno(const ff_t *ff);

/**
 * Last error message recorded by the engine. Empty string when
 * ff_errno() returns FF_OK. Owned by the engine.
 * @param ff Engine instance.
 */
const char *ff_strerror(const ff_t *ff);

/**
 * Source line on which the last error occurred (1-based when reading
 * a file via ff_load(), 0 when evaluating an interactive line).
 * @param ff Engine instance.
 */
int ff_err_line(const ff_t *ff);

/**
 * Byte offset within the offending line at which the last error
 * occurred.
 * @param ff Engine instance.
 */
int ff_err_pos(const ff_t *ff);

/**
 * printf-style output through the platform-provided vprintf callback.
 * No-op (returns 0) if no callback was registered.
 *
 * @param ff  Engine instance.
 * @param fmt printf format string.
 * @param ... Format arguments.
 * @return Number of bytes the callback reported having written.
 */
int ff_printf(ff_t *ff, const char *fmt, ...) FF_PRINTF_FMT(2, 3);

/**
 * Record a diagnostic and route it to the appropriate channel:
 * - When @p e carries FF_SEV_ERROR: stash the formatted message in
 *   ff_strerror()-readable storage, set FF_STATE_ERROR, and return @p e.
 * - Otherwise (warning/trace): forward through the platform's vtracef
 *   callback (if any) — non-error severities are not retained.
 *
 * @param ff  Engine instance.
 * @param e   Combined severity bit and FF_ERR_* code.
 * @param fmt printf format string.
 * @param ... Format arguments.
 * @return The same @p e that was passed in, for convenient
 *         `return ff_tracef(...)` in word implementations.
 */
ff_error_t ff_tracef(ff_t *ff, ff_error_t e, const char *fmt, ...) FF_PRINTF_FMT(3, 4);
