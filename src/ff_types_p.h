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
typedef uint32_t ff_uint_t;     /**< Unsigned cell (for logical shifts / bit ops). */
typedef float    ff_real_t;     /**< Floating cell value (32-bit build). */
#define FF_INT_MIN   INT32_MIN  /**< Most-negative cell value. */
#define FF_INT_MAX   INT32_MAX  /**< Most-positive cell value. */
#define FF_CELL_BITS 32         /**< Width of a cell in bits. */
#else
typedef int64_t  ff_int_t;      /**< Signed cell-sized integer (64-bit build, default). */
typedef uint64_t ff_uint_t;     /**< Unsigned cell (for logical shifts / bit ops). */
typedef double   ff_real_t;     /**< Floating cell value (64-bit build, default). */
#define FF_INT_MIN   INT64_MIN  /**< Most-negative cell value. */
#define FF_INT_MAX   INT64_MAX  /**< Most-positive cell value. */
#define FF_CELL_BITS 64         /**< Width of a cell in bits. */
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
