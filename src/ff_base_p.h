/**
 * @file ff_base_p.h
 * @brief Numeric base used when formatting numbers for output.
 *
 * The interpreter exposes Forth's `decimal` and `hex` words which
 * flip between these two values; output words (`.`, `?`, …) read the
 * field to pick the matching printf format.
 */

#pragma once

/**
 * @enum ff_base
 * @brief Print/parse base for integer values.
 *
 * Stored in @ref ff::base. Forth tradition allows arbitrary bases; ff
 * supports only the two most common ones because every numeric word
 * branches on this enum directly.
 */
typedef enum ff_base
{
    FF_BASE_DEC = 10,   /**< Decimal — selected by the `decimal` word. */
    FF_BASE_HEX = 16    /**< Hexadecimal — selected by the `hex` word. */
} ff_base_t;
