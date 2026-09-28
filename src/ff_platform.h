/**
 * @file ff_platform.h
 * @brief Platform callbacks the engine uses for I/O and diagnostics,
 *        and the limits a host sets on the Forth code it runs.
 *
 * ff never prints on its own: the embedder supplies an ff_platform_t at
 * ff_new() time, and the engine routes every byte of normal output
 * through @ref vprintf and every diagnostic above FF_SEV_ERROR through
 * @ref vtracef. (Hard errors are not forwarded — they are stashed for
 * ff_strerror().) What Forth code may reach beyond the engine — files,
 * commands, memory — the host decides here too.
 */

#pragma once

#include <ff_error.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


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
 * @brief What Forth code can reach outside the engine. A host running
 *        code it doesn't trust withholds these in @ref ff_platform::deny.
 *
 * A denied word raises -21 (FF_ERR_UNSUPPORTED) when it runs. Builds
 * configured with `FF_WITH_SYSTEM=OFF` or `FF_WITH_FILES=OFF` leave the
 * words out altogether.
 */
typedef enum ff_capability
{
    FF_CAP_SYSTEM = 1u << 0,    /**< `system`: run a command. */
    FF_CAP_FILES  = 1u << 1,    /**< `fopen` and the other file words, `stdin` / `stdout` / `stderr`. */
    FF_CAP_LOAD   = 1u << 2,    /**< `load`: evaluate a source file. */
    FF_CAP_ALL    = FF_CAP_SYSTEM | FF_CAP_FILES | FF_CAP_LOAD
} ff_capability_t;

/**
 * @brief Opens the file Forth code asked for, for `fopen` and `load`
 *        (and ff_load()), in place of the C library's fopen().
 *
 * Lets a host confine file access — to one directory, to a list of
 * names — or serve files from memory (`fmemopen`, `fopencookie`).
 *
 * @param ctx  Embedder context (see ff_vprintf_fn).
 * @param path File name as the program gave it.
 * @param mode fopen() mode string.
 * @return The open stream, or NULL (with errno set) to refuse.
 */
typedef FILE *(*ff_open_file_fn)(void *ctx, const char *path, const char *mode);

/**
 * @brief Runs the command given to `system`, in place of the C
 *        library's system().
 *
 * @param ctx Embedder context (see ff_vprintf_fn).
 * @param cmd Command line.
 * @return Status pushed for the program, as system() would return it.
 */
typedef int (*ff_run_command_fn)(void *ctx, const char *cmd);


/**
 * @struct ff_platform
 * @brief Bundle of callbacks and limits given to ff_new().
 *
 * A NULL @ref vprintf turns the engine's print path into a no-op; a
 * NULL @ref vtracef silently drops non-error diagnostics; a NULL
 * @ref watchdog disables the opcode-budget polling check (the
 * async ff_request_abort flag still works regardless). Every field
 * left zero keeps the default, so a host names only what it sets.
 */
typedef struct ff_platform
{
    void *context;             /**< Opaque pointer threaded back through every callback. */
    ff_vprintf_fn vprintf;     /**< Output callback; if NULL, ff_printf() returns 0. */
    ff_vtracef_fn vtracef;     /**< Trace/warning callback; may be NULL. */
    ff_watchdog_fn watchdog;   /**< Watchdog callback; if NULL, no polling check. */
    uint32_t watchdog_interval;/**< Opcodes between watchdog calls; 0 = engine default (65536). */
    uint32_t deny;             /**< OR of FF_CAP_* withheld from Forth code; 0 withholds nothing. */
    size_t mem_limit;          /**< Most memory, in bytes, the engine holds for the words and strings Forth
                                    code creates; beyond it an allocation raises -8 (FF_ERR_HEAP_OVER).
                                    0 = no limit. */
    ff_open_file_fn open_file; /**< Opens files for Forth code; if NULL, fopen(). */
    ff_run_command_fn run_command; /**< Runs `system` commands; if NULL, system(). */
} ff_platform_t;
