/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
/*======
This file is part of PerconaFT.


Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License, version 2,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.

----------------------------------------

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License, version 3,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.
======= */

#ident "Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved."

#include <string.h>
#include <memory.h>
#include <toku_assert.h>
#include "mempool.h"

/* Contract:
 * Caller allocates mempool struct as convenient for caller, but memory used for data storage
 * must be dynamically allocated via toku_malloc().
 * Caller dynamically allocates memory for mempool and initializes mempool by calling toku_mempool_init().
 * Once a buffer is assigned to a mempool (via toku_mempool_init()), the mempool owns it and
 * is responsible for destroying it when the mempool is destroyed.
 * Caller destroys mempool by calling toku_mempool_destroy().
 *
 * Note, toku_mempool_init() does not allocate the memory because sometimes the caller will already have
 * the memory allocated and will assign the pre-allocated memory to the mempool.
 */

/* This is a constructor to be used when the memory for the mempool struct has been
 * allocated by the caller, but no memory has yet been allocatd for the data.
 */
void toku_mempool_zero(struct mempool *mp) {
    // printf("mempool_zero %p\n", mp);
    memset(mp, 0, sizeof(*mp));
}

// TODO 4050 this is dirty, try to replace all uses of this
void toku_mempool_init(struct mempool *mp, void *base, size_t free_offset, size_t size) {
    // printf("mempool_init %p %p %lu\n", mp, base, size);
    paranoid_invariant(base != 0);
    paranoid_invariant(size < (1U<<31)); // used to be assert(size >= 0), but changed to size_t so now let's make sure it's not more than 2GB...
    paranoid_invariant(free_offset <= size);
    mp->base = base;
    mp->size = size;
    mp->free_offset = free_offset;             // address of first available memory
    mp->frag_size = 0;               // byte count of wasted space (formerly used, no longer used or available)
}

/* allocate memory and construct mempool
 */
void toku_mempool_construct(struct mempool *mp, size_t data_size) {
    if (data_size) {
        // add 25% slack
        size_t mp_size = data_size + (data_size / 4);
        mp->base = toku_xmalloc_aligned(64, mp_size);
        mp->size = mp_size;
        mp->free_offset = 0;
        mp->frag_size = 0;
    }
    else {
        toku_mempool_zero(mp);
    }
}

void toku_mempool_reset(struct mempool *mp) {
    mp->free_offset = 0;
    mp->frag_size = 0;
}

void toku_mempool_realloc_larger(struct mempool *mp, size_t data_size) {
    invariant(data_size >= mp->free_offset);

    size_t mpsize = data_size + (data_size/4);     // allow 1/4 room for expansion (would be wasted if read-only)
    void* newmem = toku_xmalloc_aligned(64, mpsize);   // allocate new buffer for mempool
    memcpy(newmem, mp->base, mp->free_offset);  // Copy old info
    toku_free(mp->base);
    mp->base = newmem;
    mp->size = mpsize;
}


void toku_mempool_destroy(struct mempool *mp) {
    // printf("mempool_destroy %p %p %lu %lu\n", mp, mp->base, mp->size, mp->frag_size);
    if (mp->base)
        toku_free(mp->base);
    toku_mempool_zero(mp);
}

void *toku_mempool_get_base(const struct mempool *mp) {
    return mp->base;
}

void *toku_mempool_get_pointer_from_base_and_offset(const struct mempool *mp, size_t offset) {
    return reinterpret_cast<void*>(reinterpret_cast<char*>(mp->base) + offset);
}

size_t toku_mempool_get_offset_from_pointer_and_base(const struct mempool *mp, const void* p) {
    paranoid_invariant(p >= mp->base);
    return reinterpret_cast<const char*>(p) - reinterpret_cast<const char*>(mp->base);
}

size_t toku_mempool_get_size(const struct mempool *mp) {
    return mp->size;
}

size_t toku_mempool_get_frag_size(const struct mempool *mp) {
    return mp->frag_size;
}

size_t toku_mempool_get_used_size(const struct mempool *mp) {
    return mp->free_offset - mp->frag_size;
}

void* toku_mempool_get_next_free_ptr(const struct mempool *mp) {
    return toku_mempool_get_pointer_from_base_and_offset(mp, mp->free_offset);
}

size_t toku_mempool_get_offset_limit(const struct mempool *mp) {
    return mp->free_offset;
}

size_t toku_mempool_get_free_size(const struct mempool *mp) {
    return mp->size - mp->free_offset;
}

size_t toku_mempool_get_allocated_size(const struct mempool *mp) {
    return mp->free_offset;
}

void *toku_mempool_malloc(struct mempool *mp, size_t size) {
    paranoid_invariant(size < (1U<<31));
    paranoid_invariant(mp->size < (1U<<31));
    paranoid_invariant(mp->free_offset < (1U<<31));
    paranoid_invariant(mp->free_offset <= mp->size);
    void *vp;
    if (mp->free_offset + size > mp->size) {
        vp = nullptr;
    } else {
        vp = reinterpret_cast<char *>(mp->base) + mp->free_offset;
        mp->free_offset += size;
    }
    paranoid_invariant(mp->free_offset <= mp->size);
    paranoid_invariant(vp == 0 || toku_mempool_inrange(mp, vp, size));
    return vp;
}

// if vp is null then we are freeing something, but not specifying what.  The data won't be freed until compression is done.
void toku_mempool_mfree(struct mempool *mp, void *vp, size_t size) {
    if (vp) { paranoid_invariant(toku_mempool_inrange(mp, vp, size)); }
    mp->frag_size += size;
    invariant(mp->frag_size <= mp->free_offset);
    invariant(mp->frag_size <= mp->size);
}


/* get memory footprint */
size_t toku_mempool_footprint(struct mempool *mp) {
    void * base = mp->base;
    size_t touched = mp->free_offset;
    size_t rval = toku_memory_footprint(base, touched);
    return rval;
}

void toku_mempool_clone(const struct mempool* orig_mp, struct mempool* new_mp) {
    new_mp->frag_size = orig_mp->frag_size;
    new_mp->free_offset = orig_mp->free_offset;
    new_mp->size = orig_mp->free_offset; // only make the cloned mempool store what is needed
    new_mp->base = toku_xmalloc_aligned(64, new_mp->size);
    memcpy(new_mp->base, orig_mp->base, new_mp->size);
}
