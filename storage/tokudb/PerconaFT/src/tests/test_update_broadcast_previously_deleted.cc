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

// test that an update broadcast called when there are previously deleted
// keys doesn't get a callback to update_fun on those keys

#include "test.h"

const int envflags = DB_INIT_MPOOL|DB_CREATE|DB_THREAD |DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN|DB_PRIVATE;

DB_ENV *env;

const int to_delete[] = { 1, 1, 0, 0, 1, 0, 0, 0, 1, 0 };
const int to_update[] = { 0, 1, 1, 1, 0, 0, 1, 0, 1, 0 };

static inline unsigned int _v(const unsigned int i) { return 10 - i; }
static inline unsigned int _e(const unsigned int i) { return i + 4; }
static inline unsigned int _u(const unsigned int v, const unsigned int e) { return v * v * e; }

static int update_fun(DB *UU(db),
                      const DBT *key,
                      const DBT *old_val, const DBT *extra,
                      void (*set_val)(const DBT *new_val,
                                      void *set_extra),
                      void *set_extra) {
    unsigned int *k, *ov, v;
    assert(key->size == sizeof(*k));
    CAST_FROM_VOIDP(k, key->data);
    assert(extra->size == 0);
    if (to_delete[*k]) {
        assert(0);
    } else {
        assert(old_val->size == sizeof(*ov));
        CAST_FROM_VOIDP(ov, old_val->data);
        v = _u(*ov, _e(*k));
    }

    if (to_update[*k]) {
        DBT newval;
        set_val(dbt_init(&newval, &v, sizeof(v)), set_extra);
    }

    return 0;
}

static void setup (void) {
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    { int chk_r = toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(chk_r); }
    { int chk_r = db_env_create(&env, 0); CKERR(chk_r); }
    env->set_errfile(env, stderr);
    env->set_update(env, update_fun);
    { int chk_r = env->open(env, TOKU_TEST_FILENAME, envflags, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(chk_r); }
}

static void cleanup (void) {
    { int chk_r = env->close(env, 0); CKERR(chk_r); }
}

static int do_inserts(DB_TXN *txn, DB *db) {
    int r = 0;
    DBT key, val;
    unsigned int i, v;
    DBT *keyp = dbt_init(&key, &i, sizeof(i));
    DBT *valp = dbt_init(&val, &v, sizeof(v));
    for (i = 0; i < (sizeof(to_update) / sizeof(to_update[0])); ++i) {
        v = _v(i);
        r = db->put(db, txn, keyp, valp, 0); CKERR(r);
    }
    return r;
}

static int do_deletes(DB_TXN *txn, DB *db) {
    int r = 0;
    DBT key;
    unsigned int i;
    DBT *keyp = dbt_init(&key, &i, sizeof(i));
    for (i = 0; i < (sizeof(to_delete) / sizeof(to_delete[0])); ++i) {
        if (to_delete[i]) {
            r = db->del(db, txn, keyp, DB_DELETE_ANY); CKERR(r);
        }
    }
    return r;
}

static int do_updates(DB_TXN *txn, DB *db, uint32_t flags) {
    DBT extra;
    DBT *extrap = dbt_init(&extra, NULL, 0);
    int r = db->update_broadcast(db, txn, extrap, flags); CKERR(r);
    return r;
}

static void chk_updated(const unsigned int k, const unsigned int v) {
    if (to_update[k]) {
        assert(v == _u(_v(k), _e(k)));
    } else {
        assert(v == _v(k));
    }
}

static void chk_original(const unsigned int k, const unsigned int v) {
    assert(v == _v(k));
}

static int do_verify_results(DB_TXN *txn, DB *db, void (*check_val)(const unsigned int k, const unsigned int v)) {
    int r = 0;
    DBT key, val;
    unsigned int i, *vp;
    DBT *keyp = dbt_init(&key, &i, sizeof(i));
    DBT *valp = dbt_init(&val, NULL, 0);
    for (i = 0; i < (sizeof(to_update) / sizeof(to_update[0])); ++i) {
        r = db->get(db, txn, keyp, valp, 0);
        if (to_delete[i]) {
            CKERR2(r, DB_NOTFOUND);
            r = 0;
        } else {
            CKERR(r);
            assert(val.size == sizeof(*vp));
            CAST_FROM_VOIDP(vp, val.data);
            check_val(i, *vp);
        }
    }
    return r;
}

static void run_test(bool is_resetting) {
    DB *db;
    uint32_t update_flags = is_resetting ? DB_IS_RESETTING_OP : 0;

    IN_TXN_COMMIT(env, NULL, txn_1, 0, {
            { int chk_r = db_create(&db, env, 0); CKERR(chk_r); }
            { int chk_r = db->open(db, txn_1, "foo.db", NULL, DB_BTREE, DB_CREATE, 0666); CKERR(chk_r); }

            { int chk_r = do_inserts(txn_1, db); CKERR(chk_r); }

            { int chk_r = do_deletes(txn_1, db); CKERR(chk_r); }

            IN_TXN_COMMIT(env, txn_1, txn_11, 0, {
                    { int chk_r = do_verify_results(txn_11, db, chk_original); CKERR(chk_r); }
                });
        });

    IN_TXN_COMMIT(env, NULL, txn_2, 0, {
            { int chk_r = do_updates(txn_2, db, update_flags); CKERR(chk_r); }
        });

    IN_TXN_COMMIT(env, NULL, txn_3, 0, {
            { int chk_r = do_verify_results(txn_3, db, chk_updated); CKERR(chk_r); }
        });

    { int chk_r = db->close(db, 0); CKERR(chk_r); }
}

int test_main(int argc, char * const argv[]) {
    parse_args(argc, argv);
    setup();
    run_test(true);
    run_test(false);
    cleanup();

    return 0;
}
