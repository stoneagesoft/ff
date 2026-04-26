/**
 * @file ff_error.h
 * @brief Error severity bits and error-code identifiers.
 *
 * An ff_error_t value is a packed integer: the high bits (above
 * FF_ERROR_SEVERITY_SHIFT) carry an FF_SEV_* severity flag, and the
 * low bits carry one of the FF_ERR_* codes. Most APIs accept the OR of
 * the two (e.g. `FF_SEV_ERROR | FF_ERR_DIV_ZERO`) and the engine
 * extracts each half on its own.
 */

#pragma once

/**
 * @brief Bit position above which severity flags live.
 *
 * Severity bits start at 1 << FF_ERROR_SEVERITY_SHIFT so that error
 * codes (which sit in the low bits) and severity flags don't overlap.
 */
#define FF_ERROR_SEVERITY_SHIFT 16

/**
 * @enum ff_error_severity
 * @brief Severity classes for diagnostics flowing through ff_tracef().
 */
typedef enum ff_error_severity
{
    FF_SEV_TRACE   = 0,                                   /**< Verbose trace output (no shifted bit). */
    FF_SEV_DEBUG   = 1 << (0 + FF_ERROR_SEVERITY_SHIFT),  /**< Engine-internal debug message. */
    FF_SEV_INFO    = 1 << (1 + FF_ERROR_SEVERITY_SHIFT),  /**< Informational notice. */
    FF_SEV_WARNING = 1 << (2 + FF_ERROR_SEVERITY_SHIFT),  /**< Recoverable warning. */
    FF_SEV_ERROR   = 1 << (3 + FF_ERROR_SEVERITY_SHIFT)   /**< Hard error: state is captured and execution halts. */
} ff_error_severity_t;

/**
 * @enum ff_error_code
 * @brief Concrete error identifiers; OR with an FF_SEV_* severity to
 *        form a full ff_error_t.
 */
typedef enum ff_error_code
{
    FF_OK = 0,              /**< No error. */

    FF_ERR_BAD_PTR,         /**< Invalid pointer dereference. */
    FF_ERR_BROKEN,          /**< Engine is in a non-recoverable broken state. */
    FF_ERR_DIV_ZERO,        /**< Integer / real division by zero. */
    FF_ERR_FILE_IO,         /**< File open / read / write failed (errno usually set). */
    FF_ERR_FORGET_PROT,     /**< Attempt to FORGET a protected (built-in) word. */
    FF_ERR_HEAP_OVER,       /**< Word heap allocation would exceed the cap. */
    FF_ERR_MALFORMED,       /**< Token sequence is structurally invalid. */
    FF_ERR_MISSING,         /**< A required token (after `'`, `s"`, …) was missing. */
    FF_ERR_NO_MAN,          /**< Word has no manual entry to display. */
    FF_ERR_NON_UNIQUE,      /**< Newly defined word shadows an existing name (warning). */
    FF_ERR_NOT_IN_DEF,      /**< Compiler-only word invoked outside a `:` definition. */
    FF_ERR_RSTACK_OVER,     /**< Return stack overflow. */
    FF_ERR_RSTACK_UNDER,    /**< Return stack underflow. */
    FF_ERR_RUN_COMMENT,     /**< Source ended inside an open `(` comment. */
    FF_ERR_RUN_STRING,      /**< Source ended inside an open string literal. */
    FF_ERR_STACK_OVER,      /**< Data stack overflow. */
    FF_ERR_STACK_UNDER,     /**< Data stack underflow. */
    FF_ERR_UNDEFINED,       /**< Word name not present in dictionary. */
    FF_ERR_UNSUPPORTED,     /**< Operation not supported on this build/platform. */

    FF_ERR_APPLICATION,     /**< Application-defined error category. */
} ff_error_code_t;

/**
 * @brief Storage type for the severity-OR-code combination.
 */
typedef unsigned ff_error_t;
