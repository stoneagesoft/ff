/**
 * @file ff_platform.h
 * @brief Platform callbacks the engine uses for I/O and diagnostics.
 *
 * ff is platform-agnostic — it never calls printf, fputs, or fprintf
 * itself. The embedder supplies an ff_platform_t at ff_new() time;
 * the engine routes every byte of normal output through @ref vprintf
 * and every diagnostic above FF_SEV_ERROR through @ref vtracef. (Hard
 * errors are not forwarded — they are stashed for ff_strerror().)
 */

#pragma once

#include <ff_error.h>

#include <stdarg.h>
#include <stdint.h>


/**
 * @brief vprintf-shaped callback for engine output.
 *
 * Called by ff_printf() (and indirectly by every Forth word that
 * prints, e.g. `.`, `cr`, `type`).
 *
 * @param ctx  The opaque @ref ff_platform::context the embedder
 *             registered, passed back unchanged.
 * @param fmt  printf format string.
 * @param args Variadic arguments matching @p fmt.
 * @return Implementation-defined byte count (typically the value
 *         returned by `vprintf` / `vfprintf`).
 */
typedef int (*ff_vprintf_fn)(void *ctx, const char *fmt, va_list args);

/**
 * @brief vprintf-shaped callback for non-error diagnostics.
 *
 * Receives traces, warnings, and informational messages tagged with
 * a severity bit (anything below FF_SEV_ERROR; hard errors are kept
 * inside the engine instead of being routed here).
 *
 * @param ctx  Embedder context (see ff_vprintf_fn).
 * @param e    Severity-tagged error code (FF_SEV_* | FF_ERR_*).
 * @param fmt  printf format string.
 * @param args Variadic arguments matching @p fmt.
 * @return Implementation-defined byte count.
 */
typedef int (*ff_vtracef_fn)(void *ctx, ff_error_t e, const char *fmt, va_list args);


/**
 * @brief Action a watchdog callback returns.
 *
 * Anything non-zero is treated as a request to abort; the engine
 * raises FF_ERR_ABORTED and unwinds back to the host.
 */
typedef enum ff_watchdog_action
{
    FF_WD_CONTINUE = 0,  /**< Keep running. */
    FF_WD_ABORT    = 1   /**< Stop now, FF_ERR_ABORTED. */
} ff_watchdog_action_t;

/**
 * @brief Periodically polled by the inner interpreter to let the host
 *        decide whether the running Forth code has run too long.
 *
 * Called every @ref ff_platform::watchdog_interval opcodes (counted
 * across back-branches and word calls — i.e. every loop iteration
 * and every nested word entry). The host is free to consult the
 * wall clock, a fuel counter, a kill-flag, etc., and return
 * FF_WD_ABORT to terminate cleanly.
 *
 * @param ctx          Embedder context (see ff_vprintf_fn).
 * @param opcodes_run  Total opcodes executed since this ff_eval() /
 *                     ff_exec() entry — useful as a cheap fuel
 *                     metric without consulting a clock.
 * @return FF_WD_CONTINUE to keep running, FF_WD_ABORT to stop.
 */
typedef ff_watchdog_action_t (*ff_watchdog_fn)(void *ctx, uint64_t opcodes_run);


/**
 * @struct ff_platform
 * @brief Bundle of callbacks given to ff_new().
 *
 * A NULL @ref vprintf turns the engine's print path into a no-op; a
 * NULL @ref vtracef silently drops non-error diagnostics; a NULL
 * @ref watchdog disables the opcode-budget polling check (the
 * async ff_request_abort flag still works regardless).
 */
typedef struct ff_platform
{
    void *context;             /**< Opaque pointer threaded back through every callback. */
    ff_vprintf_fn vprintf;     /**< Output callback; if NULL, ff_printf() returns 0. */
    ff_vtracef_fn vtracef;     /**< Trace/warning callback; may be NULL. */
    ff_watchdog_fn watchdog;   /**< Watchdog callback; if NULL, no polling check. */
    uint32_t watchdog_interval;/**< Opcodes between watchdog calls; 0 = engine default (65536). */
} ff_platform_t;
