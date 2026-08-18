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
#include <ff_version.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


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
 * @return Newly allocated engine, owned by the caller (release with
 *         ff_free()), or NULL if the initial allocation fails. This is
 *         the one allocation the library reports rather than faulting on;
 *         deeper compile-time allocations follow a fail-fast policy.
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
 * @return FF_OK on success, otherwise one of the bare FF_ERR_* codes
 *         declared in ff_error.h — directly comparable, e.g.
 *         `if (ff_eval(ff, s) == FF_ERR_DIV_ZERO)`. The severity bit is
 *         masked off the return; recover it, if ever needed, from the
 *         value passed to a `vtracef` callback via FF_ERR_SEV().
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
 *
 * Returns the bare FF_ERR_* code, directly comparable against the
 * constants in ff_error.h (the severity bit is masked off).
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


/* ===================================================================
 * Version
 * =================================================================== */

/**
 * @return The library version string (e.g. "1.0.0"). The matching
 *         compile-time macros FF_VERSION / FF_VERSION_MAJOR / … come
 *         from <ff_version.h>, included above; compare them to detect a
 *         header/runtime mismatch.
 */
const char *ff_version(void);


/* ===================================================================
 * Thread-safety warm-up
 * =================================================================== */

/**
 * Initialize the process-wide shared built-in table up front.
 *
 * ff_new() builds this table lazily on the first call, and that lazy
 * initialization is not thread-safe. A host that creates engines from
 * multiple threads must call ff_warmup() once from a single thread
 * before spawning them; afterwards any number of threads may call
 * ff_new() concurrently, since the shared table is then read-only.
 * Idempotent.
 */
void ff_warmup(void);


/* ===================================================================
 * Native word registration
 *
 * Lets a host expose C functions as Forth words using only this public
 * header — no internal headers required. Inside a word function, use the
 * data-stack accessors below (or the advanced <ff_p.h> API) to read
 * arguments and push results.
 * =================================================================== */

/** @brief A native word: receives the engine, works on its data stack. */
typedef void (*ff_word_fn)(ff_t *ff);

/**
 * @struct ff_native_word
 * @brief One row of a NULL-terminated native-word registration table.
 */
typedef struct ff_native_word
{
    const char *name;       /**< Forth name; NULL terminates the table. */
    bool        immediate;  /**< true = compile-time (immediate) word. */
    ff_word_fn  fn;         /**< Implementation. */
    const char *manual;     /**< Optional help text (first line = prototype); may be NULL. */
} ff_native_word_t;

/** @brief Table row for an ordinary native word. */
#define FF_NATIVE(name, fn, man)    { (name), false, (fn), (man) }
/** @brief Table row for an immediate native word. */
#define FF_NATIVE_I(name, fn, man)  { (name), true,  (fn), (man) }
/** @brief Sentinel terminating a native-word table. */
#define FF_NATIVE_END               { NULL, false, NULL, NULL }

/**
 * Register a NULL-terminated table of native words into @p ff.
 * @param ff    Engine instance.
 * @param words Table terminated by FF_NATIVE_END.
 * @return FF_OK.
 */
ff_error_t ff_register(ff_t *ff, const ff_native_word_t *words);


/* ===================================================================
 * Data-stack marshaling
 *
 * For hosts that drive Forth words from C — push arguments, run a word
 * via ff_eval()/ff_exec(), pop results. Values are exchanged as int64_t
 * / double; on a 32-bit-cell build (FF_32BIT) integer values are
 * narrowed to the cell width.
 * =================================================================== */

/** @return Number of cells currently on the data stack. */
size_t ff_depth(const ff_t *ff);

/** Push an integer. @return false if the stack is full (nothing pushed). */
bool ff_push_int(ff_t *ff, int64_t v);
/** Pop an integer into @p out. @return false if the stack is empty. */
bool ff_pop_int(ff_t *ff, int64_t *out);
/** Push a real. @return false if the stack is full (nothing pushed). */
bool ff_push_real(ff_t *ff, double v);
/** Pop a real into @p out. @return false if the stack is empty. */
bool ff_pop_real(ff_t *ff, double *out);
