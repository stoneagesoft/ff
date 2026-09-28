/**
 * @file ff_throw_p.h
 * @brief THROW codes: the values an exception carries through the engine
 *        and that `catch`, `evaluate` and `load` leave on the stack.
 *
 * Every error is an exception. An engine error raised through
 * ff_tracef() carries the ANS Forth code for its FF_ERR_* (Table 9.1)
 * where one exists — stack underflow is -4, an undefined word -13 — and
 * `-(256 + code)` from the system-defined range otherwise. A THROW from
 * Forth code carries whatever code the program gave it.
 *
 * Two codes cannot be caught: FF_THROW_INTERRUPT (the watchdog or
 * ff_request_abort(), which untrusted code must not be able to swallow)
 * and FF_THROW_QUIT. They unwind to the outermost evaluation.
 */

#pragma once


/**
 * @enum ff_throw
 * @brief The ANS THROW codes the engine raises or treats specially.
 */
typedef enum ff_throw
{
    FF_THROW_ABORT         =   -1,  /**< `abort`. Uncaught: the engine is reset. */
    FF_THROW_ABORTQ        =   -2,  /**< `abort"`. Uncaught: as ABORT, with its message. */
    FF_THROW_STACK_OVER    =   -3,  /**< FF_ERR_STACK_OVER. */
    FF_THROW_STACK_UNDER   =   -4,  /**< FF_ERR_STACK_UNDER. */
    FF_THROW_RSTACK_OVER   =   -5,  /**< FF_ERR_RSTACK_OVER. */
    FF_THROW_RSTACK_UNDER  =   -6,  /**< FF_ERR_RSTACK_UNDER. */
    FF_THROW_DICT_OVER     =   -8,  /**< FF_ERR_HEAP_OVER. */
    FF_THROW_BAD_ADDRESS   =   -9,  /**< FF_ERR_BAD_PTR. */
    FF_THROW_DIV_ZERO      =  -10,  /**< FF_ERR_DIV_ZERO. */
    FF_THROW_UNDEFINED     =  -13,  /**< FF_ERR_UNDEFINED. */
    FF_THROW_COMPILE_ONLY  =  -14,  /**< FF_ERR_NOT_IN_DEF. */
    FF_THROW_BAD_FORGET    =  -15,  /**< FF_ERR_FORGET_PROT. */
    FF_THROW_UNSUPPORTED   =  -21,  /**< FF_ERR_UNSUPPORTED. */
    FF_THROW_CS_MISMATCH   =  -22,  /**< Control structure mismatch: an unmatched, misnested or
                                         unclosed `if` / `begin` / `do` / `{`. */
    FF_THROW_RSTACK_IMBAL  =  -25,  /**< FF_ERR_SCOPE_RSTACK. */
    FF_THROW_INTERRUPT     =  -28,  /**< Watchdog / ff_request_abort(). Not catchable. */
    FF_THROW_NESTING       =  -29,  /**< Compiler nesting: `:` while a definition is open. */
    FF_THROW_FILE_IO       =  -37,  /**< FF_ERR_FILE_IO. */
    FF_THROW_QUIT          =  -56,  /**< `quit`. Not catchable; not an error either. */
    FF_THROW_ALLOCATE      =  -59,  /**< FF_ERR_OOM. */
    FF_THROW_SYSTEM_BASE   = -256   /**< FF_ERR_* with no ANS code: -(256 + code). */
} ff_throw_t;
