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

#include <toku_assert.h>

void concurrent_tree::create(const comparator *cmp) {
    // start with an empty root node. we do this instead of
    // setting m_root to null so there's always a root to lock
    m_root.create_root(cmp);
}

void concurrent_tree::destroy(void) {
    m_root.destroy_root();
}

bool concurrent_tree::is_empty(void) {
    return m_root.is_empty();
}

uint64_t concurrent_tree::get_insertion_memory_overhead(void) {
    return sizeof(treenode);
}

void concurrent_tree::locked_keyrange::prepare(concurrent_tree *tree) {
    // the first step in acquiring a locked keyrange is locking the root
    treenode *const root = &tree->m_root;
    m_tree = tree;
    m_subtree = root;
    m_range = keyrange::get_infinite_range();
    root->mutex_lock();
}

void concurrent_tree::locked_keyrange::acquire(const keyrange &range) {
    treenode *const root = &m_tree->m_root;

    treenode *subtree;
    if (root->is_empty() || root->range_overlaps(range)) {
        subtree = root;
    } else {
        // we do not have a precomputed comparison hint, so pass null
        const keyrange::comparison *cmp_hint = nullptr;
        subtree = root->find_node_with_overlapping_child(range, cmp_hint);
    }

    // subtree is locked. it will be unlocked when this is release()'d
    invariant_notnull(subtree);
    m_range = range;
    m_subtree = subtree;
}

void concurrent_tree::locked_keyrange::release(void) {
    m_subtree->mutex_unlock();
}

template <class F>
void concurrent_tree::locked_keyrange::iterate(F *function) const {
    // if the subtree is non-empty, traverse it by calling the given
    // function on each range, txnid pair found that overlaps.
    if (!m_subtree->is_empty()) {
        m_subtree->traverse_overlaps(m_range, function);
    }
}

void concurrent_tree::locked_keyrange::insert(const keyrange &range, TXNID txnid) {
    // empty means no children, and only the root should ever be empty
    if (m_subtree->is_empty()) {
        m_subtree->set_range_and_txnid(range, txnid);
    } else {
        m_subtree->insert(range, txnid);
    }
}

void concurrent_tree::locked_keyrange::remove(const keyrange &range) {
    invariant(!m_subtree->is_empty());
    treenode *new_subtree = m_subtree->remove(range);
    // if removing range changed the root of the subtree,
    // then the subtree must be the root of the entire tree.
    if (new_subtree == nullptr) {
        invariant(m_subtree->is_root());
        invariant(m_subtree->is_empty());
    }
}

void concurrent_tree::locked_keyrange::remove_all(void) {
    m_subtree->recursive_remove();
}
