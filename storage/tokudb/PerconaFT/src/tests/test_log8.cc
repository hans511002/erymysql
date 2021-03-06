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

#include "test.h"


/* Test to see if we can do logging and recovery. */
/* This is very specific to PerconaFT.  It won't work with Berkeley DB. */
/* This test_log8 inserts to a db, closes, reopens, and inserts more to db.  We want to make sure that the recovery of the buffers works. */


#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>

// TOKU_TEST_FILENAME is defined in the Makefile

struct in_db;
struct in_db {
    long int r;
    int i;
    struct in_db *next;
} *items=0;

int maxcount = 10;

static void insert_some (int outeri, bool close_env) {
    uint32_t create_flag = outeri%2 ? DB_CREATE : 0; // Sometimes use DB_CREATE, sometimes don't.
    int r;
    DB_ENV *env;
    DB *db;
    DB_TXN *tid;
    r=db_env_create(&env, 0); assert(r==0);
    db_env_enable_engine_status(0);  // disable engine status on crash because test is expected to fail
    
    r=env->open(env, TOKU_TEST_FILENAME, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE|create_flag, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);

    r=db_create(&db, env, 0); CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, create_flag, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=tid->commit(tid, 0);    assert(r==0);

    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    
    int i;
    for (i=0; i<maxcount; i++) {
	char hello[30], there[30];
	DBT key,data;
	struct in_db *XMALLOC(newitem);
	newitem->r = random();
	newitem->i = i;
	newitem->next = items;
	items = newitem;
	snprintf(hello, sizeof(hello), "hello%ld.%d.%d", newitem->r, outeri, newitem->i);
	snprintf(there, sizeof(hello), "there%d", i);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data  = hello; key.size=strlen(hello)+1;
	data.data = there; data.size=strlen(there)+1;
	r=db->put(db, tid, &key, &data, 0);  assert(r==0);
    }
    r=tid->commit(tid, 0);    assert(r==0);
    r=db->close(db, 0);       assert(r==0);
    if (close_env) {
        r=env->close(env, 0);     assert(r==0);
    }
}    

static void make_db (bool close_env) {
    DB_ENV *env;
    DB *db;
    DB_TXN *tid;
    int r;
    int i;

    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    r=toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    db_env_enable_engine_status(0);  // disable engine status on crash because test is expected to fail
    
    r=env->open(env, TOKU_TEST_FILENAME, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=tid->commit(tid, 0);    assert(r==0);
    r=db->close(db, 0);  CKERR(r);
    if (close_env) {
        r=env->close(env, 0); CKERR(r);
    }

    for (i=0; i<1; i++)
	insert_some(i, close_env);
    
    while (items) {
	struct in_db *next=items->next;
	toku_free(items);
	items=next;
    }
}

int
test_main (int argc, char *const argv[]) {
    bool close_env = true;
    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "--no-shutdown") == 0)
            close_env = false;
    }
    make_db(close_env);
    return 0;
}
