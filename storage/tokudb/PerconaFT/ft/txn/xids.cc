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

/* Purpose of this file is to implement xids list of nested transactions
 * ids.
 *
 * See design documentation for nested transactions at
 * TokuWiki/Imp/TransactionsOverview.
 *
 * NOTE: xids are always stored in disk byte order.  
 *       Accessors are responsible for transposing bytes to 
 *       host order.
 */

#include <errno.h>
#include <string.h>

#include "portability/memory.h"
#include "portability/toku_assert.h"
#include "portability/toku_htod.h"
#include "portability/toku_portability.h"

#include "ft/txn/xids.h"

/////////////////////////////////////////////////////////////////////////////////
//  This layer of abstraction (xids_xxx) understands xids<> and nothing else.
//  It contains all the functions that understand xids<>
//
//  xids<> do not store the implicit transaction id of 0 at index 0.
//  The accessor functions make the id of 0 explicit at index 0.
//  The number of xids physically stored in the xids array is in
//  the variable num_xids.
//
// The xids struct is immutable.  The caller gets an initial version of XIDS
// by calling toku_xids_get_root_xids(), which returns the constant struct
// representing the root transaction (id 0).  When a transaction begins, 
// a new XIDS is created with the id of the current transaction appended to
// the list.
// 
//

// This is the xids list for a transactionless environment.
// It is also the initial state of any xids list created for
// nested transactions.

XIDS
toku_xids_get_root_xids(void) {
    static const struct XIDS_S root_xids = {
        .num_xids = 0
    };

    XIDS rval = (XIDS)&root_xids;
    return rval;
}

bool 
toku_xids_can_create_child(XIDS xids) {
    invariant(xids->num_xids < MAX_TRANSACTION_RECORDS);
    return (xids->num_xids + 1) != MAX_TRANSACTION_RECORDS;
}

int
toku_xids_create_unknown_child(XIDS parent_xids, XIDS *xids_p) {
    // Postcondition:
    //  xids_p points to an xids that is an exact copy of parent_xids, but with room for one more xid.
    int rval;
    invariant(parent_xids);
    uint32_t num_child_xids = parent_xids->num_xids + 1;
    // assumes that caller has verified that num_child_xids will
    // be less than MAX_TRANSACTIN_RECORDS
    invariant(num_child_xids < MAX_TRANSACTION_RECORDS);
    size_t new_size = sizeof(*parent_xids) + num_child_xids*sizeof(parent_xids->ids[0]);
    XIDS CAST_FROM_VOIDP(xids, toku_xmalloc(new_size));
    // Clone everything (parent does not have the newest xid).
    memcpy(xids, parent_xids, new_size - sizeof(xids->ids[0]));
    *xids_p = xids;
    rval = 0;
    return rval;
}

void
toku_xids_finalize_with_child(XIDS xids, TXNID this_xid) {
    // Precondition:
    //  - xids was created by toku_xids_create_unknown_child
    TXNID this_xid_disk = toku_htod64(this_xid);
    uint32_t num_child_xids = ++xids->num_xids;
    xids->ids[num_child_xids - 1] = this_xid_disk;
}

// xids is immutable.  This function creates a new xids by copying the
// parent's list and then appending the xid of the new transaction.
int
toku_xids_create_child(XIDS parent_xids,		// xids list for parent transaction
                       XIDS *xids_p,		// xids list created
                       TXNID this_xid) {		// xid of this transaction (new innermost)
    bool can_create_child = toku_xids_can_create_child(parent_xids);
    if (!can_create_child) {
        return EINVAL;
    }
    toku_xids_create_unknown_child(parent_xids, xids_p);
    toku_xids_finalize_with_child(*xids_p, this_xid);
    return 0;
}

void
toku_xids_create_from_buffer(struct rbuf *rb,		// xids list for parent transaction
                             XIDS *xids_p) {		// xids list created
    uint8_t num_xids = rbuf_char(rb);
    invariant(num_xids < MAX_TRANSACTION_RECORDS);
    XIDS CAST_FROM_VOIDP(xids, toku_xmalloc(sizeof(*xids) + num_xids*sizeof(xids->ids[0])));
    xids->num_xids = num_xids;
    uint8_t index;
    for (index = 0; index < xids->num_xids; index++) {
        rbuf_TXNID(rb, &xids->ids[index]);
    }
    *xids_p = xids;
}

void
toku_xids_destroy(XIDS *xids_p) {
    if (*xids_p != toku_xids_get_root_xids()) toku_free(*xids_p);
    *xids_p = NULL;
}

// Return xid at requested position.  
// If requesting an xid out of range (which will be the case if xids array is empty)
// then return 0, the xid of the root transaction.
TXNID 
toku_xids_get_xid(XIDS xids, uint8_t index) {
    invariant(index < toku_xids_get_num_xids(xids));
    TXNID rval = xids->ids[index];
    rval = toku_dtoh64(rval);
    return rval;
}

uint8_t 
toku_xids_get_num_xids(XIDS xids) {
    uint8_t rval = xids->num_xids;
    return rval;
}

// Return innermost xid 
TXNID 
toku_xids_get_innermost_xid(XIDS xids) {
    TXNID rval = TXNID_NONE;
    if (toku_xids_get_num_xids(xids)) {
        // if clause above makes this cast ok
        uint8_t innermost_xid = (uint8_t) (toku_xids_get_num_xids(xids) - 1);
        rval = toku_xids_get_xid(xids, innermost_xid);
    }
    return rval;
}

TXNID
toku_xids_get_outermost_xid(XIDS xids) {
    TXNID rval = TXNID_NONE;
    if (toku_xids_get_num_xids(xids)) {
        rval = toku_xids_get_xid(xids, 0);
    }
    return rval;
}

void
toku_xids_cpy(XIDS target, XIDS source) {
    size_t size = toku_xids_get_size(source);
    memcpy(target, source, size);
}

// return size in bytes
uint32_t 
toku_xids_get_size(XIDS xids) {
    uint32_t rval;
    uint8_t num_xids = xids->num_xids;
    rval = sizeof(*xids) + num_xids * sizeof(xids->ids[0]);
    return rval;
}

uint32_t 
toku_xids_get_serialize_size(XIDS xids) {
    uint32_t rval;
    uint8_t num_xids = xids->num_xids;
    rval = 1 + //num xids
           8 * num_xids;
    return rval;
}

unsigned char *
toku_xids_get_end_of_array(XIDS xids) {
    TXNID *r = xids->ids + xids->num_xids;
    return (unsigned char*)r;
}

void wbuf_nocrc_xids(struct wbuf *wb, XIDS xids) {
    wbuf_nocrc_char(wb, (unsigned char)xids->num_xids);
    uint8_t index;
    for (index = 0; index < xids->num_xids; index++) {
        wbuf_nocrc_TXNID(wb, xids->ids[index]);
    }
}

void
toku_xids_fprintf(FILE *fp, XIDS xids) {
    uint8_t index;
    unsigned num_xids = toku_xids_get_num_xids(xids);
    fprintf(fp, "[|%u| ", num_xids);
    for (index = 0; index < toku_xids_get_num_xids(xids); index++) {
        if (index) fprintf(fp, ",");
        fprintf(fp, "%" PRIx64, toku_xids_get_xid(xids, index));
    }
    fprintf(fp, "]");
}

