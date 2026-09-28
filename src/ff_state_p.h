/**
 * @file ff_state_p.h
 * @brief Engine-wide state flags stored in @ref ff::state.
 *
 * Each flag tracks one mode bit. Flags are OR'd into @ref ff::state and
 * consulted by the inner interpreter, the tokenizer, and individual word
 * case bodies. Words that take a name or a string from the input stream
 * (`:`, `'`, `."`, …) read it themselves when they run, so no flag
 * carries a request over to the next token; only a `{` signature, which
 * may span lines, keeps a mode between tokens.
 */

#pragma once

/**
 * @enum ff_state
 * @brief OR-able state flags for @ref ff::state.
 */
typedef enum ff_state
{
    FF_STATE_COMPILING      = 1 <<  0,  /**< Compile state: tokens are compiled into ff::compiling. `[`
                                             clears it while the definition stays open. */
    FF_STATE_TRACE          = 1 <<  7,  /**< Trace each word entry through ff_tracef(). */
    FF_STATE_BACKTRACE      = 1 <<  8,  /**< Push to the back-trace stack on every word entry. */
    FF_STATE_THROWN         = 1 << 13,  /**< An exception is in flight (an error, THROW, ABORT, QUIT or a
                                             watchdog abort; code in ff::throw_code). Execution unwinds until
                                             a `catch`, `evaluate` / `load`, or the outermost API call
                                             settles it. */
    FF_STATE_SIG_PENDING    = 1 << 14   /**< `{` is collecting its `( a b -- c )` signature tokens. */
} ff_state_t;
