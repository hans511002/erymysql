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

#include "portability/memory.h"
#include "portability/toku_portability.h"

#include "ft/serialize/block_table.h"
#include "ft/ft-internal.h"
#include "ft/serialize/ft_node-serialize.h"
#include "ft/txn/rollback.h"
#include "ft/txn/rollback-ct-callbacks.h"

#include "util/memarena.h"

// Address used as a sentinel. Otherwise unused.
static struct serialized_rollback_log_node cloned_rollback;

// Cleanup the rollback memory
static void
rollback_log_destroy(ROLLBACK_LOG_NODE log) {
    make_rollback_log_empty(log);
    toku_free(log);
}

// flush an ununused log to disk, by allocating a size 0 blocknum in
// the blocktable
static void toku_rollback_flush_unused_log(ROLLBACK_LOG_NODE log,
                                           BLOCKNUM logname,
                                           int fd,
                                           FT ft,
                                           bool write_me,
                                           bool keep_me,
                                           bool for_checkpoint,
                                           bool is_clone) {
    if (write_me) {
        DISKOFF offset;
        ft->blocktable.realloc_on_disk(
            logname, 0, &offset, ft, fd, for_checkpoint);
    }
    if (!keep_me && !is_clone) {
        toku_free(log);
    }
}

// flush a used log to disk by serializing and writing the node out
static void
toku_rollback_flush_used_log (
    ROLLBACK_LOG_NODE log,
    SERIALIZED_ROLLBACK_LOG_NODE serialized,
    int fd,
    FT ft,
    bool write_me,
    bool keep_me,
    bool for_checkpoint,
    bool is_clone
    )
{

    if (write_me) {
        int r = toku_serialize_rollback_log_to(fd, log, serialized, is_clone, ft, for_checkpoint);
        assert(r == 0);
    }
    if (!keep_me) {
        if (is_clone) {
            toku_serialized_rollback_log_destroy(serialized);
        }
        else {
            rollback_log_destroy(log);
        }
    }
}

// Write something out.  Keep trying even if partial writes occur.
// On error: Return negative with errno set.
// On success return nbytes.
void toku_rollback_flush_callback (
    CACHEFILE UU(cachefile),
    int fd,
    BLOCKNUM logname,
    void *rollback_v,
    void** UU(disk_data),
    void *extraargs,
    PAIR_ATTR size,
    PAIR_ATTR* new_size,
    bool write_me,
    bool keep_me,
    bool for_checkpoint,
    bool is_clone
    )
{
    ROLLBACK_LOG_NODE log = nullptr;
    SERIALIZED_ROLLBACK_LOG_NODE serialized = nullptr;
    bool is_unused = false;
    if (is_clone) {
        is_unused = (rollback_v == &cloned_rollback);
        CAST_FROM_VOIDP(serialized, rollback_v);
    }
    else {
        CAST_FROM_VOIDP(log, rollback_v);
        is_unused = rollback_log_is_unused(log);
    }
    *new_size = size;
    FT ft;
    CAST_FROM_VOIDP(ft, extraargs);
    if (is_unused) {
        toku_rollback_flush_unused_log(
            log,
            logname,
            fd,
            ft,
            write_me,
            keep_me,
            for_checkpoint,
            is_clone
            );
    }
    else {
        toku_rollback_flush_used_log(
            log,
            serialized,
            fd,
            ft,
            write_me,
            keep_me,
            for_checkpoint,
            is_clone
            );
    }
}

int toku_rollback_fetch_callback (CACHEFILE cachefile, PAIR p, int fd, BLOCKNUM logname, uint32_t fullhash UU(),
                                 void **rollback_pv,  void** UU(disk_data), PAIR_ATTR *sizep, int * UU(dirtyp), void *extraargs) {
    int r;
    FT CAST_FROM_VOIDP(h, extraargs);
    assert(h->cf == cachefile);
    ROLLBACK_LOG_NODE *result = (ROLLBACK_LOG_NODE*)rollback_pv;
    r = toku_deserialize_rollback_log_from(fd, logname, result, h);
    if (r==0) {
        (*result)->ct_pair = p;
        *sizep = rollback_memory_size(*result);
    }
    return r;
}

void toku_rollback_pe_est_callback(
    void* rollback_v, 
    void* UU(disk_data),
    long* bytes_freed_estimate, 
    enum partial_eviction_cost *cost, 
    void* UU(write_extraargs)
    )
{
    assert(rollback_v != NULL);
    *bytes_freed_estimate = 0;
    *cost = PE_CHEAP;
}

// callback for partially evicting a cachetable entry
int toku_rollback_pe_callback (
    void *rollback_v, 
    PAIR_ATTR old_attr, 
    void* UU(extraargs),
    void (*finalize)(PAIR_ATTR new_attr, void * extra),
    void *finalize_extra
    ) 
{
    assert(rollback_v != NULL);
    finalize(old_attr, finalize_extra);
    return 0;
}

// partial fetch is never required for a rollback log node
bool toku_rollback_pf_req_callback(void* UU(ftnode_pv), void* UU(read_extraargs)) {
    return false;
}

// a rollback node should never be partial fetched, 
// because we always say it is not required.
// (pf req callback always returns false)
int toku_rollback_pf_callback(void* UU(ftnode_pv),  void* UU(disk_data), void* UU(read_extraargs), int UU(fd), PAIR_ATTR* UU(sizep)) {
    assert(false);
    return 0;
}

// the cleaner thread should never choose a rollback node for cleaning
int toku_rollback_cleaner_callback (
    void* UU(ftnode_pv),
    BLOCKNUM UU(blocknum),
    uint32_t UU(fullhash),
    void* UU(extraargs)
    )
{
    assert(false);
    return 0;
}

void toku_rollback_clone_callback(
    void* value_data,
    void** cloned_value_data,
    long* clone_size,
    PAIR_ATTR* new_attr,
    bool UU(for_checkpoint),
    void* UU(write_extraargs)
    )
{
    ROLLBACK_LOG_NODE CAST_FROM_VOIDP(log, value_data);
    SERIALIZED_ROLLBACK_LOG_NODE serialized = nullptr;
    if (!rollback_log_is_unused(log)) {
        XMALLOC(serialized);
        toku_serialize_rollback_log_to_memory_uncompressed(log, serialized);
        *cloned_value_data = serialized;
        *clone_size = sizeof(struct serialized_rollback_log_node) + serialized->len;
    }
    else {
        *cloned_value_data = &cloned_rollback;
        *clone_size = sizeof(cloned_rollback);
    }
    // clear the dirty bit, because the node has been cloned
    log->dirty = 0;
    new_attr->is_valid = false;
}

