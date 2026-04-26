/**
 * @file ff_types_p.h
 * @brief Fundamental cell-sized integer and real types.
 *
 * The data and return stacks, the heap, and the bytecode operands all
 * use a single uniform cell type @ref ff_int_t. Real numbers are
 * stored in cells via memcpy of @ref ff_real_t. By default cells are
 * 64-bit; defining @c FF_32BIT at build time selects 32-bit cells (and
 * single-precision reals) for embedded targets.
 */

#pragma once

#include <stdint.h>

#ifdef FF_32BIT
typedef int32_t  ff_int_t;      /**< Signed cell-sized integer (32-bit build). */
typedef float    ff_real_t;     /**< Floating cell value (32-bit build). */
#else
typedef int64_t  ff_int_t;      /**< Signed cell-sized integer (64-bit build, default). */
typedef double   ff_real_t;     /**< Floating cell value (64-bit build, default). */
#endif

/**
 * @brief Forth-style boolean constants.
 *
 * Forth conventionally represents true as all-bits-set (-1) so that
 * AND/OR with a flag does the expected mask. Comparison words and the
 * Forth runtime use these values.
 */
enum {
    FF_TRUE  = -1,  /**< Boolean true (all bits set). */
    FF_FALSE =  0   /**< Boolean false. */
};
