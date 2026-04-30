/**
 * @file ff_p.h
 * @brief Engine private structure and umbrella header.
 *
 * Include this from a custom native-word source file: it pulls in
 * every @c *_p.h internal layout plus the public API headers, exposes
 * the @ref ff struct definition, and provides the same hot-path inline
 * accessors and validation macros that the engine itself uses.
 *
 * Symbols and types declared here are advanced internals; their
 * layouts may change between releases.
 */

#pragma once

/* Public headers */
#include <ff.h>
#include <ff_error.h>
#include <ff_platform.h>

/* Internal definitions */
#include <ff_base_p.h>
#include <ff_bt_stack_p.h>
#include <ff_config_p.h>
#include <ff_dict_p.h>
#include <ff_heap_p.h>
#include <ff_opcode_p.h>
#include <ff_stack_p.h>
#include <ff_state_p.h>
#include <ff_tok_state_p.h>
#include <ff_token_p.h>
#include <ff_tokenizer_p.h>
#include <ff_types_p.h>
#include <ff_word_def_p.h>
#include <ff_word_flags_p.h>
#include <ff_word_p.h>

#include <signal.h>
#include <stdint.h>
#include <string.h>


/* ===================================================================
 * Async abort flag — set by ff_request_abort (which may be called
 * from a signal handler or another thread) and polled by the inner
 * interpreter at every back-branch and word call.
 *
 * On a C11 toolchain with atomics support we use atomic_int with
 * release/acquire semantics, so a setter on one CPU is observable
 * to the polling loop on another within bounded latency on weakly-
 * ordered architectures (ARM, RISC-V). On older toolchains and on
 * MSVC builds without C11 atomics we fall back to
 * `volatile sig_atomic_t` — same-thread (signal-handler) safety
 * still holds; cross-thread visibility relies on the implementation.
 * =================================================================== */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L \
        && !defined(__STDC_NO_ATOMICS__)
#  include <stdatomic.h>
   typedef atomic_int ff_abort_flag_t;
#  define FF_ABORT_LOAD(p)    atomic_load_explicit((p), memory_order_acquire)
#  define FF_ABORT_STORE(p,v) atomic_store_explicit((p), (v), memory_order_release)
#  define FF_ABORT_CLEAR(p)   atomic_store_explicit((p), 0, memory_order_relaxed)
#else
   typedef volatile sig_atomic_t ff_abort_flag_t;
#  define FF_ABORT_LOAD(p)    (*(p))
#  define FF_ABORT_STORE(p,v) (*(p) = (v))
#  define FF_ABORT_CLEAR(p)   (*(p) = 0)
#endif


/**
 * @struct ff
 * @brief The interpreter instance.
 *
 * Owns every per-instance resource: platform callbacks, error state,
 * dictionary, stacks, IP, scratch pads, tokenizer, current input
 * buffer, numeric base, and the in-flight word pointer.
 */
struct ff
{
    ff_platform_t platform;             /**< Embedder-supplied I/O callbacks. */

    ff_state_t state;                   /**< OR of FF_STATE_* mode and pending flags. */
    ff_error_t error;                   /**< Last error code (FF_OK if none). */
    char error_msg[FF_ERROR_MSG_SIZE];  /**< Most recent error message. */
    int error_line;                     /**< Source line of the last error. */
    int error_pos;                      /**< Byte position within the line of the last error. */

    ff_dict_t dict;                     /**< Dictionary of words. */

    ff_stack_t stack;                   /**< Data stack. */
    ff_stack_t r_stack;                 /**< Return stack. */
    ff_bt_stack_t bt_stack;             /**< Back-trace stack (when FF_STATE_BACKTRACE is on). */
    ff_int_t *ip;                       /**< Current instruction pointer (NULL when not running). */

    /* Bump arena for transient interpret-time strings. Strings are
       appended forward; the arena grows on demand and is reset only
       by ff_abort. See FF_PAD_INIT_SIZE in ff_config_p.h for the
       lifetime contract. */
    char  *pad_buf;                     /**< Heap-allocated arena base; NULL until first use. */
    size_t pad_used;                    /**< Bytes consumed from @ref pad_buf. */
    size_t pad_size;                    /**< Allocated capacity of @ref pad_buf. */

    ff_tokenizer_t tokenizer;           /**< Persistent lexer state. */
    const char *input;                  /**< Currently-tokenizing source buffer. */
    int input_pos;                      /**< Cursor into @ref input. */

    ff_base_t base;                     /**< Numeric input/output base. */

    ff_word_t *cur_word;                /**< Word currently being executed (for diagnostics). */

    ff_int_t throw_code;                /**< Exception code stashed by THROW; read by the matching CATCH. */

    /* Watchdog state. `abort_requested` is set asynchronously (by
       ff_request_abort, possibly from a signal handler or another
       thread) and polled by the inner interpreter at every
       back-branch and word call. See FF_ABORT_LOAD / FF_ABORT_STORE
       above for the memory-order contract. `opcodes_run` is the
       running opcode count consulted by the polling watchdog
       callback; reset on each ff_exec entry. */
    ff_abort_flag_t abort_requested;
    uint64_t        opcodes_run;
    uint64_t        next_watchdog_at;
};


/* ===================================================================
 * Branch-prediction hints. The validators below all guard the rare
 * "something went wrong" path; tagging the comparison with
 * ff_unlikely() lets GCC/Clang lay out the hot path straight-line and
 * push the diagnostic-and-return code into a cold section.
 * =================================================================== */
#if defined(__GNUC__) || defined(__clang__)
#  define ff_unlikely(x) __builtin_expect(!!(x), 0)
#  define ff_likely(x)   __builtin_expect(!!(x), 1)
#else
#  define ff_unlikely(x) (x)
#  define ff_likely(x)   (x)
#endif


/* ===================================================================
 * Validation macros (word-fn context).
 *
 * Each macro is a statement that may raise an error and `return;` from
 * the enclosing word function, so they cannot be inline functions. Put
 * them at the top of a word body before touching the stack or trying to
 * compile. The `do { ... } while (0)` wrapper makes each expansion a
 * single statement so that `if (cond) FF_SL(ff, 2);` parses as the
 * reader expects.
 *
 * The dispatch-context variants (`_FF_SL` etc.) live inside ff_exec
 * and `goto done` instead of returning, since ff_exec is a bool fn
 * that has stack/state cleanup at the end.
 * =================================================================== */

/**
 * @brief Stack-underflow check; returns from the calling function on miss.
 * @param e Engine pointer.
 * @param n Required minimum stack depth.
 */
#define FF_SL(e, n) \
    do { \
        if (ff_unlikely((int)(e)->stack.top < (int)(n))) \
        { \
            ff_tracef(e, FF_SEV_ERROR | FF_ERR_STACK_UNDER, \
                      "Stack underflow: %d item(s) expected.", (int)(n)); \
            return; \
        } \
    } while (0)

/**
 * @brief Stack-overflow check; returns from the calling function on miss.
 * @param e Engine pointer.
 * @param n Number of cells the caller is about to push.
 */
#define FF_SO(e, n) \
    do { \
        if (ff_unlikely((int)(e)->stack.top + (int)(n) > FF_STACK_SIZE)) \
        { \
            ff_tracef(e, FF_SEV_ERROR | FF_ERR_STACK_OVER, \
                      "Stack overflow: %d item(s) would not fit.", (int)(n)); \
            return; \
        } \
    } while (0)

/**
 * @brief Return-stack underflow check.
 * @param e Engine pointer.
 * @param n Required minimum return-stack depth.
 */
#define FF_RSL(e, n) \
    do { \
        if (ff_unlikely((int)(e)->r_stack.top < (int)(n))) \
        { \
            ff_tracef(e, FF_SEV_ERROR | FF_ERR_RSTACK_UNDER, \
                      "Return stack underflow: %d item(s) expected.", (int)(n)); \
            return; \
        } \
    } while (0)

/**
 * @brief Return-stack overflow check.
 * @param e Engine pointer.
 * @param n Number of cells the caller is about to push to R.
 */
#define FF_RSO(e, n) \
    do { \
        if (ff_unlikely((int)(e)->r_stack.top + (int)(n) > FF_STACK_SIZE)) \
        { \
            ff_tracef(e, FF_SEV_ERROR | FF_ERR_RSTACK_OVER, \
                      "Return stack overflow: %d item(s) would not fit.", (int)(n)); \
            return; \
        } \
    } while (0)

/**
 * @brief "Must be in compile mode" check; returns on miss.
 * @param e Engine pointer.
 */
#define FF_COMPILING(e) \
    do { \
        if (ff_unlikely(!((e)->state & FF_STATE_COMPILING))) \
        { \
            ff_tracef(e, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF, \
                      "Compiler word outside definition."); \
            return; \
        } \
    } while (0)


/* ===================================================================
 * Memory-safety checks (gated by FF_SAFE_MEM).
 *
 * `ff_addr_valid` returns true when [addr, addr + bytes) lies entirely
 * inside one of the engine's tracked regions: the data stack, the
 * return stack, the pad ring, or any dictionary word's heap. Ranges
 * are checked against capacity (not size), so a `here`-derived
 * pointer into freshly-allotted but as-yet-unwritten cells is
 * accepted.
 *
 * `ff_word_valid` returns true when @p w is currently in the
 * dictionary — the relevant question for `execute`.
 *
 * Both run in O(dict.count) — the data plane uses a linear scan
 * rather than a sorted-interval index, on the theory that the check
 * fires only when FF_SAFE_MEM is on and dictionaries are small in
 * the embedded use cases that flip it on.
 * =================================================================== */

/**
 * @brief Out-of-line dictionary-interval validator (slow path).
 *
 * Called by @ref ff_addr_valid below when the inline fast paths
 * (stacks + pad) miss. Embedders should normally call ff_addr_valid;
 * this is exposed mainly so its signature matches the inline.
 */
bool ff_addr_valid_dict(const ff_t *ff, const void *addr, size_t bytes);

bool ff_word_valid(const ff_t *ff, const ff_word_t *w);

/**
 * @brief Range-validate @p addr / @p bytes against this engine's
 *        tracked regions: stacks, pad, dictionary heaps.
 *
 * Inline because the stack and pad cases dominate hot-path checks
 * under FF_SAFE_MEM — folding the range comparisons into the
 * caller lets the compiler hoist them out of inner loops. The
 * dict-intervals binary search stays out-of-line so call sites
 * don't bloat.
 */
static inline bool ff_addr_valid(const ff_t *ff, const void *addr, size_t bytes)
{
    if (addr == NULL || bytes == 0)
        return false;

    const char *a   = (const char *)addr;
    const char *end = a + bytes;
    /* Pointer wrap — always invalid. */
    if (ff_unlikely(end < a))
        return false;

    /* Data stack: inline range check. */
    {
        const char *lo = (const char *)ff->stack.data;
        const char *hi = lo + sizeof(ff->stack.data);
        if (a >= lo && end <= hi)
            return true;
    }

    /* Return stack: inline range check. */
    {
        const char *lo = (const char *)ff->r_stack.data;
        const char *hi = lo + sizeof(ff->r_stack.data);
        if (a >= lo && end <= hi)
            return true;
    }

    /* Pad bump arena (live prefix only). */
    if (ff->pad_buf && ff->pad_used > 0)
    {
        const char *lo = ff->pad_buf;
        const char *hi = lo + ff->pad_used;
        if (a >= lo && end <= hi)
            return true;
    }

    /* Dictionary heaps fall through to the out-of-line binary search. */
    return ff_addr_valid_dict(ff, addr, bytes);
}

/**
 * @def FF_CHECK_ADDR
 * @brief Verify @p addr / @p bytes lies in a tracked region; raise
 *        FF_ERR_BAD_PTR and return from the calling word fn on miss.
 *
 * Compiles to a no-op when FF_SAFE_MEM is 0.
 */
/**
 * @def FF_CHECK_XT
 * @brief Verify @p w is a live dictionary entry; raise FF_ERR_BAD_PTR
 *        and return from the calling word fn on miss.
 */
#if FF_SAFE_MEM
#  define FF_CHECK_ADDR(e, addr, bytes) \
       do { \
           if (ff_unlikely(!ff_addr_valid((e), (addr), (size_t)(bytes)))) \
           { \
               ff_tracef(e, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                         "Bad pointer: %p (size %zu).", \
                         (const void *)(addr), (size_t)(bytes)); \
               return; \
           } \
       } while (0)
#  define FF_CHECK_XT(e, w) \
       do { \
           if (ff_unlikely(!ff_word_valid((e), (w)))) \
           { \
               ff_tracef(e, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                         "Bad execution token: %p.", (const void *)(w)); \
               return; \
           } \
       } while (0)
#else
#  define FF_CHECK_ADDR(e, addr, bytes) ((void)0)
#  define FF_CHECK_XT(e, w)             ((void)0)
#endif


/* ===================================================================
 * Engine-level stack accessors.
 *
 * Each returns a pointer to the stack element. Dereference to read or
 * write, e.g. `*ff_s0(ff) = v;`. At -O1+ the compiler inlines these
 * away, emitting the same machine code as the previous macros.
 * =================================================================== */

/** @return Pointer to data stack TOS (depth 0). */
static inline ff_int_t *ff_s0(ff_t *ff) { return ff_tos(&ff->stack); }
/** @return Pointer to data stack NOS (depth 1). */
static inline ff_int_t *ff_s1(ff_t *ff) { return ff_nos(&ff->stack); }
/** @return Pointer to data stack at depth 2. */
static inline ff_int_t *ff_s2(ff_t *ff) { return ff_sat(&ff->stack, 2); }
/** @return Pointer to data stack at depth 3. */
static inline ff_int_t *ff_s3(ff_t *ff) { return ff_sat(&ff->stack, 3); }
/** @return Pointer to data stack at depth 4. */
static inline ff_int_t *ff_s4(ff_t *ff) { return ff_sat(&ff->stack, 4); }
/** @return Pointer to data stack at depth 5. */
static inline ff_int_t *ff_s5(ff_t *ff) { return ff_sat(&ff->stack, 5); }

/** @return Pointer to return stack TOS. */
static inline ff_int_t *ff_r0(ff_t *ff) { return ff_tos(&ff->r_stack); }
/** @return Pointer to return stack NOS. */
static inline ff_int_t *ff_r1(ff_t *ff) { return ff_nos(&ff->r_stack); }
/** @return Pointer to return stack at depth 2. */
static inline ff_int_t *ff_r2(ff_t *ff) { return ff_sat(&ff->r_stack, 2); }
/** @return Pointer to return stack at depth 3. */
static inline ff_int_t *ff_r3(ff_t *ff) { return ff_sat(&ff->r_stack, 3); }


/* ===================================================================
 * Real number cell access via memcpy bit-pattern.
 * =================================================================== */

/**
 * Read a real value out of a cell address.
 * @param p Cell to interpret as bit-pattern.
 * @return The decoded real.
 */
static inline ff_real_t ff_get_real(ff_int_t *p)
{
    ff_real_t r;
    memcpy(&r, p, sizeof(r));
    return r;
}

/**
 * Write a real value's bit-pattern into a cell.
 * @param p Cell to overwrite.
 * @param r Real to encode.
 */
static inline void ff_set_real(ff_int_t *p, ff_real_t r)
{
    memcpy(p, &r, sizeof(r));
}

/** @brief Read a real out of TOS. */
static inline ff_real_t ff_real0(ff_t *ff) { return ff_get_real(ff_s0(ff)); }
/** @brief Read a real out of NOS. */
static inline ff_real_t ff_real1(ff_t *ff) { return ff_get_real(ff_s1(ff)); }
/** @brief Read a real out of depth-2. */
static inline ff_real_t ff_real2(ff_t *ff) { return ff_get_real(ff_s2(ff)); }

/** @brief Write a real to TOS. */
static inline void ff_set_real0(ff_t *ff, ff_real_t r) { ff_set_real(ff_s0(ff), r); }
/** @brief Write a real to NOS. */
static inline void ff_set_real1(ff_t *ff, ff_real_t r) { ff_set_real(ff_s1(ff), r); }
