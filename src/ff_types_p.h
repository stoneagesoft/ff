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

#include <inttypes.h>
#include <stdint.h>

#ifdef FF_32BIT
typedef int32_t  ff_int_t;      /**< Signed cell-sized integer (32-bit build). */
typedef uint32_t ff_uint_t;     /**< Unsigned cell (for logical shifts / bit ops). */
typedef float    ff_real_t;     /**< Floating cell value (32-bit build). */
#define FF_INT_MIN   INT32_MIN  /**< Most-negative cell value. */
#define FF_INT_MAX   INT32_MAX  /**< Most-positive cell value. */
#define FF_CELL_BITS 32         /**< Width of a cell in bits. */
#define FF_PRIdCELL  PRId32     /**< printf conversion for a signed cell. */
#define FF_PRIxCELL  PRIx32     /**< printf conversion for a cell as lowercase hex. */
#define FF_PRIXCELL  PRIX32     /**< printf conversion for a cell as uppercase hex. */
#else
typedef int64_t  ff_int_t;      /**< Signed cell-sized integer (64-bit build, default). */
typedef uint64_t ff_uint_t;     /**< Unsigned cell (for logical shifts / bit ops). */
typedef double   ff_real_t;     /**< Floating cell value (64-bit build, default). */
#define FF_INT_MIN   INT64_MIN  /**< Most-negative cell value. */
#define FF_INT_MAX   INT64_MAX  /**< Most-positive cell value. */
#define FF_CELL_BITS 64         /**< Width of a cell in bits. */
#define FF_PRIdCELL  PRId64     /**< printf conversion for a signed cell. */
#define FF_PRIxCELL  PRIx64     /**< printf conversion for a cell as lowercase hex. */
#define FF_PRIXCELL  PRIX64     /**< printf conversion for a cell as uppercase hex. */
#endif

/* A cell must be able to hold a pointer: the engine stores return
   addresses, word pointers, and native fn pointers inside cells. This
   fails the build loudly (rather than crashing at run time) if FF_32BIT
   is selected on a target whose pointers are wider than 32 bits. */
_Static_assert(sizeof(ff_int_t) >= sizeof(void *),
               "ff cell (ff_int_t) must be at least pointer-sized; "
               "FF_32BIT is only valid on 32-bit-pointer targets.");

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
