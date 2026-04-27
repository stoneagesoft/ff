/**
 * @file ff_state_p.h
 * @brief Engine-wide state flags stored in @ref ff::state.
 *
 * Each flag tracks one mode bit or pending-token signal. Flags are
 * OR'd into @ref ff::state and consulted by the inner interpreter,
 * the tokenizer, and individual word case bodies. Pending-* flags
 * survive only until the next consumed token; mode flags
 * (`COMPILING`, `TRACE`, `BACKTRACE`) persist across tokens.
 */

#pragma once

/**
 * @enum ff_state
 * @brief OR-able state flags for @ref ff::state.
 */
typedef enum ff_state
{
    FF_STATE_COMPILING      = 1 <<  0,  /**< Inside a `:` colon-definition. */
    FF_STATE_DEF_PENDING    = 1 <<  1,  /**< Next token is a name to assign to the just-created definition placeholder. */
    FF_STATE_FORGET_PENDING = 1 <<  2,  /**< Next token names a word to remove via FORGET. */
    FF_STATE_IS_PENDING     = 1 << 12,  /**< `is`: pop xt from data stack and store at next-token-named deferred word. */
    FF_STATE_TICK_PENDING   = 1 <<  3,  /**< Top-level `'` is waiting for the next-line word name. */
    FF_STATE_CTICK_PENDING  = 1 <<  4,  /**< Compile-time `[']`: emit the next word's address as a literal. */
    FF_STATE_CBRACK_PENDING = 1 <<  5,  /**< Compile-time `[compile]`: compile the next word non-immediate. */
    FF_STATE_STRLIT_ANTIC   = 1 <<  6,  /**< Tokenizer should treat the next string as inline literal data. */
    FF_STATE_TRACE          = 1 <<  7,  /**< Trace each word entry through ff_tracef(). */
    FF_STATE_BACKTRACE      = 1 <<  8,  /**< Push to the back-trace stack on every word entry. */
    FF_STATE_BROKEN         = 1 <<  9,  /**< Engine entered a non-recoverable state mid-execution. */
    FF_STATE_ABORTED        = 1 << 10,  /**< ff_abort() was invoked; transient flag for the host loop. */
    FF_STATE_ERROR          = 1 << 11,  /**< Most recent ff_eval / word raised an error (see ff_errno). */
    FF_STATE_THROWN         = 1 << 13   /**< THROW raised; the matching CATCH absorbs and clears this. */
} ff_state_t;
