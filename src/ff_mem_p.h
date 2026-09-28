/**
 * @file ff_mem_p.h
 * @brief The memory an engine holds on behalf of Forth code, measured
 *        against the host's limit (ff_platform::mem_limit).
 *
 * Counted: the slabs word storage is carved from, the words themselves
 * with their names, and the slabs of the transient string arena — the
 * allocations whose size Forth code decides. Fixed-size engine state
 * (stacks, tables) is not.
 *
 * An allocation the limit or the host allocator refuses fails softly:
 * it is recorded here and the operation that needed it does nothing.
 * The engine turns the record into an exception at its next check,
 * before anything relies on the missing memory.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>


/**
 * @struct ff_mem
 * @brief Allocation account of one engine.
 */
typedef struct ff_mem
{
    size_t used;        /**< Bytes held for Forth code. */
    size_t limit;       /**< Most @ref used may reach; 0 = no limit. */
    bool   failed;      /**< An allocation was refused since the engine last raised one. */
    bool   oom;         /**< ...by the host allocator, rather than by the limit. */
    size_t refused;     /**< Size of that allocation, in bytes. */
} ff_mem_t;


/**
 * @brief Whether @p bytes more would stay within the limit.
 * @param m     Account.
 * @param bytes Size of the allocation.
 */
static inline bool ff_mem_fits(const ff_mem_t *m, size_t bytes)
{
    return m->limit == 0 || (m->used <= m->limit && bytes <= m->limit - m->used);
}

/**
 * @brief Bytes that can still be allocated; SIZE_MAX with no limit.
 * @param m Account.
 */
static inline size_t ff_mem_room(const ff_mem_t *m)
{
    if (m->limit == 0)
        return (size_t)-1;
    return m->used < m->limit ? m->limit - m->used : 0;
}

/**
 * @brief Record an allocation of @p bytes that was refused.
 * @param m     Account.
 * @param bytes Size asked for.
 * @param oom   True if the host allocator refused it, false if the limit
 *              (or a size that doesn't fit in memory at all) did.
 */
static inline void ff_mem_refuse(ff_mem_t *m, size_t bytes, bool oom)
{
    if (m->failed)
        return;     /* the first refusal is the one to report */
    m->failed  = true;
    m->oom     = oom;
    m->refused = bytes;
}

/** @brief Count @p bytes as held. */
static inline void ff_mem_charge(ff_mem_t *m, size_t bytes)
{
    m->used += bytes;
}

/** @brief Count @p bytes as given back. */
static inline void ff_mem_release(ff_mem_t *m, size_t bytes)
{
    m->used = bytes < m->used ? m->used - bytes : 0;
}
