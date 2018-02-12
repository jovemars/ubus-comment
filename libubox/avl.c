/*
 * PacketBB handler library (see RFC 5444)
 * Copyright (c) 2010 Henning Rogge <hrogge@googlemail.com>
 * Original OLSRd implementation by Hannes Gredler <hannes@gredler.at>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org/git for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include "avl.h"
#include "list.h"

/**
 * internal type save inline function to calculate the maximum of
 * to integers without macro implementation.
 *
 * @param x first parameter of maximum function
 * @param y second parameter of maximum function
 * @return largest integer of both parameters
 */
static inline int avl_max(int x, int y) {
    return x > y ? x : y;
}

/**
 * internal type save inline function to calculate the minimum of
 * to integers without macro implementation.
 *
 * @param x first parameter of minimum function
 * @param y second parameter of minimum function
 * @return smallest integer of both parameters
 */
static inline int avl_min(int x, int y) {
    return x < y ? x : y;
}

static struct avl_node *
avl_find_rec(struct avl_node *node, const void *key, avl_tree_comp comp, void *ptr, int *cmp_result);
static void avl_insert_before(struct avl_tree *tree, struct avl_node *pos_node, struct avl_node *node);
static void avl_insert_after(struct avl_tree *tree, struct avl_node *pos_node, struct avl_node *node);
static void post_insert(struct avl_tree *tree, struct avl_node *node);
static void avl_delete_worker(struct avl_tree *tree, struct avl_node *node);
static void avl_remove(struct avl_tree *tree, struct avl_node *node);

/**
 * Initialize a new avl_tree struct
 * @param tree pointer to avl-tree
 * @param comp pointer to comparator for the tree
 * @param allow_dups true if the tree allows multiple
 *        elements with the same key, false otherwise.
 * @param ptr custom parameter for comparator
 */
void
avl_init(struct avl_tree *tree, avl_tree_comp comp, bool allow_dups, void *ptr)
{
    // (&tree->list_head)->next = (&tree->list_head)->prev = &tree->list_head;
    //  ----->    <----
    // |     _|__|_    |
    // |___ | next |   |
    //      |______|   |
    //      | prev |___|
    //      |______|
    //
    INIT_LIST_HEAD(&tree->list_head); // linked list
    tree->root = NULL; // root node of avl tree
    tree->count = 0;   // number of avl tree
    tree->comp = comp; // comparator
    tree->allow_dups = allow_dups; // true if allow nodes with same key
    tree->cmp_ptr = ptr; // custom data for comparator
}

/**
 * find the node after the given node
 */
static inline struct avl_node *avl_next(struct avl_node *node)
{
        // return the actual address of an avl_node structure
        // which contains of the node refered to by node->list.next
        return list_entry(node->list.next, struct avl_node, list); 
}

/**
 * Finds a node in an avl-tree with a certain key
 * @param tree pointer to avl-tree
 * @param key pointer to key
 * @return pointer to avl-node with key, NULL if no node with
 *    this key exists.
 */
struct avl_node *
avl_find(const struct avl_tree *tree, const void *key)
{
    struct avl_node *node;
    int diff;

    if (tree->root == NULL)
        return NULL;

    node = avl_find_rec(tree->root, key, tree->comp, tree->cmp_ptr, &diff);

    return diff == 0 ? node : NULL;
}

/**
 * Finds the last node in an avl-tree with a key less or equal
 * than the specified key
 * @param tree pointer to avl-tree
 * @param key pointer to specified key
 * @return pointer to avl-node, NULL if no node with
 *    key less or equal specified key exists.
 */
struct avl_node *
avl_find_lessequal(const struct avl_tree *tree, const void *key) {
    struct avl_node *node, *next;
    int diff;

    if (tree->root == NULL)
        return NULL;

    node = avl_find_rec(tree->root, key, tree->comp, tree->cmp_ptr, &diff);

    /* go left as long as key<node.key */
    while (diff < 0) {
        if (list_is_first(&node->list, &tree->list_head)) {
            return NULL;
        }

        node = (struct avl_node *)node->list.prev;
        diff = (*tree->comp) (key, node->key, tree->cmp_ptr);
    }

    /* go right as long as key>=next_node.key */
    next = node;
    while (diff >= 0) {
        node = next;
        if (list_is_last(&node->list, &tree->list_head)) {
            break;
        }

        next = (struct avl_node *)node->list.next;
        diff = (*tree->comp) (key, next->key, tree->cmp_ptr);
    }
    return node;
}

/**
 * Finds the first node in an avl-tree with a key greater or equal
 * than the specified key
 * @param tree pointer to avl-tree
 * @param key pointer to specified key
 * @return pointer to avl-node, NULL if no node with
 *    key greater or equal specified key exists.
 */
struct avl_node *
avl_find_greaterequal(const struct avl_tree *tree, const void *key) {
    struct avl_node *node, *next;
    int diff;

    if (tree->root == NULL)
        return NULL;

    node = avl_find_rec(tree->root, key, tree->comp, tree->cmp_ptr, &diff);

    /* go right as long as key>node.key */
    while (diff > 0) {
        if (list_is_last(&node->list, &tree->list_head)) {
            return NULL;
        }

        node = (struct avl_node *)node->list.next;
        diff = (*tree->comp) (key, node->key, tree->cmp_ptr);
    }

    /* go left as long as key<=next_node.key */
    next = node;
    while (diff <= 0) {
        node = next;
        if (list_is_first(&node->list, &tree->list_head)) {
            break;
        }

        next = (struct avl_node *)node->list.prev;
        diff = (*tree->comp) (key, next->key, tree->cmp_ptr);
    }
    return node;
}

/**
 * Inserts an avl_node into a tree
 * @param tree pointer to tree
 * @param new pointer to node
 * @return 0 if node was inserted successfully, -1 if it was not inserted
 *   because of a key collision
 */
int
avl_insert(struct avl_tree *tree, struct avl_node *new)
{
    struct avl_node *node, *next, *last;
    int diff;

    /*
     * 1. 如果当前树和链表没有节点,则tree->root=new, tree->list_head->next=new->list;
     * 2. 查找整棵树，直到找到一个节点node:
     *    1) key等于new,则将new置于链表中所有相同key的节点的最后面,但不插入到树里;
     *    2) key大于new并且左子节点为空,将new插入到链表中node的前面,树中node的左子树;
     *    3) key小于new并且右子节点为空,将new插入到链表的最后面,树中node的右子树.
     * 3. 插入完成以后,要调整node及其以上节点的平衡因子,以及树节点总数
     */

    /** initialize the new node as an isolated node */
    new->parent = NULL;

    new->left = NULL;
    new->right = NULL;

    new->balance = 0;
    new->leader = true;

    /** if there is no node in the avl tree */
    
    if (tree->root == NULL) {
        // add new node to the list maitenanced by avl_tree
        list_add(&new->list, &tree->list_head);
        // set new node as the root node
        tree->root = new;
        tree->count = 1;
        return 0;
    }

    /** if there were nodes in the avl tree already */
    
    // The return node is either with the same key as new node, or a leaf node.
    // node要么是第一个与new的key值相等的节点,
    // 要么就是key值大于new并且左子节点为空的节点,
    // 要么就是key值小于new并且右子节点为空的节点
    node = avl_find_rec(tree->root, new->key, tree->comp, tree->cmp_ptr, &diff);

    last = node;

    // iterate the list from the node found by avl_find_rec() to find the last node with the same key as node
    // wont break unless find a node which is the first of a series nodes with same key
    // because the new node is either the same as current node in key, or a child node
    // last要么是一系列key相等的节点的最后一个，要么是整个链表的最后一个
    while (!list_is_last(&last->list, &tree->list_head)/* (&last->list)->next == &tree->list_head */) {
        // find the next node who contains the last->list.next as a member
        next = avl_next(last);

        // next avl node is the first of series nodes with same key
        if (next->leader) {
            break;
        }
        last = next;
    }

    // WTF???
    diff = (*tree->comp) (new->key, node->key, tree->cmp_ptr);

    /** IF NEW NODE KEY IS DUPLICATE, NO NEED INSERT TO AVL TREE */

    // new key is same with node
    if (diff == 0) {

        // dup keys is not allowed
        if (!tree->allow_dups)
            return -1;

        // false since not the first
        new->leader = 0;

        // add the new node to link list but not insert to avl tree
        avl_insert_after(tree, last, new); // ==> list_add(&new->list, &last->list);
                                                                             //    tree->count++;
        return 0;
    }

    /** TO KEEP BALANCE FACTOR NEVER MORE THAN ONE IS PRIOR */
    // 到这里说明node的key值一定不等于new的key值,并且至少有一侧为空,
    // 而为空的一侧正是new要插入的位置,插入new以后node重归平衡
    // right heavy, left hole is the the position
    if (node->balance == 1) {
        // insert new node before the first dup nodes
        avl_insert_before(tree, node, new);

        node->balance = 0;
        new->parent = node;
        node->left = new;
        return 0;
    }

    // left heavy, right hole is the the position
    if (node->balance == -1) {
        // insert new node after the last dup nodes
        avl_insert_after(tree, last, new);

        node->balance = 0;
        new->parent = node;
        node->right = new;
        return 0;
    }

    /** SINCE NODE IS BALANCE, INSERT MUST BE RESEARCGABLE */
    // 到这里说明node是一个叶子结点,插入一个新子节点将破坏其平衡性
    // new key is smaller than node, insert left
    if (diff < 0) {
        // node 
        avl_insert_before(tree, node, new);

        // set balance factor to -1 after insert
        node->balance = -1;

        // new is left child of node
        new->parent = node;
        node->left = new;

        // update parent balance factor, then rebalance
        post_insert(tree, node);
        return 0;
    }

    // new key is bigger than node, insert right
    // last is the tail of list
    avl_insert_after(tree, last, new);

    node->balance = 1;
    new->parent = node;
    node->right = new;
    post_insert(tree, node);
    return 0;
}

/**
 * Remove a node from an avl tree
 * @param tree pointer to tree
 * @param node pointer to node
 */
void
avl_delete(struct avl_tree *tree, struct avl_node *node)
{
    struct avl_node *next;
    struct avl_node *parent;
    struct avl_node *left;
    struct avl_node *right;

    /**
     * If the node is the first of a series of nodes with same key, and
     * the next node in the list is not the first of another series, 
     * then the next node should be set to take the place of the node;
     * otherwise, remove it from list only.
     * If the node has no same key node, remove it from both avl-tree and list
     */

    if (node->leader) { // true if node is the first of a series of nodes with same key
        if (tree->allow_dups // true if nodes with same key are allowed
            && !list_is_last(&node->list, &tree->list_head) // node is not the last of list
            && !(next = avl_next(node))->leader) { // next behind the node in the list is not another first of dup key nodes

            // next should inhabit the place of node
            next->leader = true;
            next->balance = node->balance;

            parent = node->parent;
            left = node->left;
            right = node->right;

            next->parent = parent;
            next->left = left;
            next->right = right;

            if (parent == NULL)
                tree->root = next;

            else {
                if (node == parent->left)
                    parent->left = next;

                else
                    parent->right = next;
            }

            if (left != NULL)
                left->parent = next;

            if (right != NULL)
                right->parent = next;
        }

        else
            avl_delete_worker(tree, node);
    }

    avl_remove(tree, node);
}

static struct avl_node *
avl_find_rec(struct avl_node *node, const void *key, 
                                    avl_tree_comp comp, void *cmp_ptr, int *cmp_result)
{
    int diff;

    // if key1 < key2, diff = -1;
    // if key1 = key2, diff =  0;
    // if key1 > key2, diff =  1;
    diff = (*comp) (key, node->key, cmp_ptr); // ubus_cmp_id(key1, key2, ptr)
    *cmp_result = diff;

    if (diff < 0) {
        // keeping looking until reach a leaf
        if (node->left != NULL)
            return avl_find_rec(node->left, key, comp, cmp_ptr, cmp_result);

        return node;
    }

    if (diff > 0) {
        // keeping looking until reach a leaf
        if (node->right != NULL)
            return avl_find_rec(node->right, key, comp, cmp_ptr, cmp_result);

        return node;
    }

    return node;
}

/* rotate to right */
static void
avl_rotate_right(struct avl_tree *tree, struct avl_node *node)
{
    struct avl_node *left, *parent;

     //                  parent                                                |--->parent
     //                   //                                                 p |      |
     //                  //                                                    |      | ?
     //                 node                                             |--->node<---|
     //                //  \\                 ===>                     p |     ||
     //               //    \\                                           |   l ||
     //             left    (node->right)                              left<---||--->(node->right)
     //            //  \\                                               ||
     //           //    \\                                            l || r
     // (left->left)     (left->right)                  (left->left)<---||---->(left->right)
    left = node->left;
    parent = node->parent;

    //   |----------->parent
    //   |             |
    // p |             | ?
    //   |    node<----|
    //   |   l ||
    //  left<--|| p
    //   ^      |
    //   |------|
    left->parent = parent;
    node->parent = left;

    if (parent == NULL)
        //  root
        //   |    node
        //   |   l ||
        //  left<--|| p
        //   ^      |
        //   |------|
        tree->root = left;

    else {
        if (parent->left == node)
            //   |----------->parent                       |----->parent
            //   |             |                         p |       |
            // p |             | l                         |       | l
            //   |    node<----|                    |---->left<----|
            //   |   l ||              ===>       p |      ^|
            //  left<--|| p                         |    l ||
            //   ^      |                           node---||----->(left->right)
            //   |------|
            parent->left = left;

        else
            //   |--->parent                       parent<----|
            //   |       |                            |       | p
            // p |       | r                        r |       | 
            //   |       |---->node                   |---->left<----|
            //   |            l ||     ===>                  |^      | p
            //  left<-----------|| p                         || l    |
            //   ^               |                         r ||-----node
            //   |---------------|                           |----->(left->right)
            parent->right = left;
    }

    //           left<----|
    //             |      | p
    //             | r    |
    //             |---->node
    //                    |
    //                    | l
    // (left->right)<-----|
    node->left = left->right;
    left->right = node;

    if (node->left != NULL)
        //       parent<----|                              parent
        //          |       | p                                \\
        //        r |       |                                   \\
        //          |---->left<----|                            left
        //               l ||      | p      ===>               //  \\
        // (left->left)<---|| r    |                          //    \\
        //              ----=====>node              (left->left)     node
        //              | p        |                                //  \\
        //              |          | l                             //    \\
        //       (left->right)<----|                    (left->right)     (node->right)
        node->left->parent = node;

    // Before:
    //    A = height(left(left))
    //    B = height(right(left))
    //    C = height(right(node))
    //    height(left) = MAX(A, B) + 1
    //    balance(left) = L = B - A
    //    balance(node) = N = C - (MAX(A, B) + 1)
    //
    // After:
    //    balance(node) = C - B
    //                  = (MAX(A, B) + 1) + N - B
    //                  = N - (B - MAX(A, B)) + 1
    //                  = N + 1 - MIN(L, 0)
    //    balance(left) = (MAX(B, C) + 1) - A
    //                  = MAX(B, MAX(A, B) + 1 + N) + 1 - A
    //                  = MAX(L, MAX(0, L) + 1 + N) + 1
    //                  = L + 1 + MAX(0, MAX(-L, 0) + 1 + N)
    //                  = L + 1 + MAX(N + 1 - MIN(L, 0), 0)
    //                  = L + 1 + MAX(balance(node), 0)
    node->balance += 1 - avl_min(left->balance, 0); // x < y ? x : y
    left->balance += 1 + avl_max(node->balance, 0); // x > y ? x : y
}

static void
avl_rotate_left(struct avl_tree *tree, struct avl_node *node)
{
    struct avl_node *right, *parent;

    //         parent                                            parent
    //            \\                                               \\
    //             \\                                               \\
    //             node                                            right
    //             // \\                     ===>                  // \\
    //            //   \\                                         //   \\
    //  (node->left)   right                                    node   (right->right)
    //                 //  \\                                   // \\
    //                //    \\                                 //   \\
    //     (right->left)    (right->right)           (node->left)   (right->left)

    right = node->right;
    parent = node->parent;

    right->parent = parent;
    node->parent = right;

    if (parent == NULL)
        tree->root = right;

    else {
        if (parent->left == node)
            parent->left = right;

        else
            parent->right = right;
    }

    node->right = right->left;
    right->left = node;

    if (node->right != NULL)
        node->right->parent = node;

    node->balance -= 1 + avl_max(right->balance, 0);
    right->balance -= 1 - avl_min(node->balance, 0);
}

/**
 * Recusively rebalance the tree after inserting after a balanced node
 */
static void
post_insert(struct avl_tree *tree, struct avl_node *node)
{
    struct avl_node *parent = node->parent;

    /** 
     * before insert, node is a leaf
     * after insert, node has a child in one side
     */

    if (parent == NULL)
        return;

    /** NODE IS LEFT CHILD OF PARENT */
    
    if (node == parent->left) {

        // since a new node has been insert under node,
        // the balance factor of the parent of node should be update
        parent->balance--;

        if (parent->balance == 0)
            // parent returns balance
            //
            //    (parent, 0)
            //          /    \
            //   (node, ±1)   (right, ±1)
            //       /         \
            // (new, 0)       (leaf, 0)
            //
            return;

        if (parent->balance == -1) {
            // parent becomes left heavy
            //
            //    (parent, -1)
            //          /    \
            //   (node, ±1)   (leaf, 0)
            //       /
            // (new, 0)
            //
            post_insert(tree, parent);
            return;
        }

        /** parent balance factor is smaller than -1 */

        if (node->balance == -1) {
            // if node and parent are left heavy, rorate right
            //    (parent, -1)               (node, 0)
            //          /                     /      \
            //   (node, -1)      ===>   (new, 0)     (parent, 0)
            //       /
            // (new, 0)
            //
            avl_rotate_right(tree, parent);
            return;
        }

        // parent is left heavy, node is right heavy
        //  (parent, -1)               (parent, -2)              (new, 0)
        //        /                        /                      /     \
        //  (node, 1)       ===>      (new, -1)       ===>   (node, 0)  (parent, 0)
        //       \                       /
        //    (new, 0)             (node, 0)
        avl_rotate_left(tree, node);
        avl_rotate_right(tree, node->parent->parent);
        return;
    }

    /** NODE IS RIGHT CHILD OF PARENT */
    
    parent->balance++;

    if (parent->balance == 0)
        return;

    if (parent->balance == 1) {
        post_insert(tree, parent);
        return;
    }

    if (node->balance == 1) {
        // (parent, 1)                     (node, 0)
        //        \                        /      \
        //     (node, 1)    ===>   (parent, 0)    (new, 0)
        //         \
        //      (new, 0)
        avl_rotate_left(tree, parent);
        return;
    }
 
    // (parent, 1)            (parent, 1)                       (node, 0)
    //        \                       \                          /     \
    //     (node, 1)    ===>       (new, 1)      ===>    (parent, 0)   (new, 0)
    //         /                        \
    //    (new, 0)                  (node, 0)
    avl_rotate_right(tree, node);
    avl_rotate_left(tree, node->parent->parent);
}

/*
 * insert node to the prev of pos_node
 */
static void
avl_insert_before(struct avl_tree *tree, struct avl_node *pos_node, struct avl_node *node)
{
    list_add_tail(&node->list, &pos_node->list); // _list_add(&node->list, (&pos_node->list)->prev, &pos_node->list);
    tree->count++;
}

/*
 * insert node to the next of pos_node
 */
static void
avl_insert_after(struct avl_tree *tree, struct avl_node *pos_node, struct avl_node *node)
{
    
    list_add(&node->list, &pos_node->list); // _list_add(&node->list, &pos_node->list, (&pos_node->list)->next)
    tree->count++;
}

static void
avl_remove(struct avl_tree *tree, struct avl_node *node)
{
    list_del(&node->list);
    tree->count--;
}

static void
avl_post_delete(struct avl_tree *tree, struct avl_node *node)
{
    struct avl_node *parent;

    /**
     * node is root
     */

    if ((parent = node->parent) == NULL)
        return;

    /**
     * node is left child
     */

    if (node == parent->left) {
        // delete the child of node may cause the parent right heavy
        parent->balance++;

        /**
         * parent is left heavy before
         */

        if (parent->balance == 0) {
            // search until found an earlier balanced parent
            avl_post_delete(tree, parent);
            return;
        }

        /**
         * parent is balance before
         */

        if (parent->balance == 1)
            return;

        /**
         * parent is right heavy before, which means
         * right subtree of parent is 2 more layers in height now, like:
         *
         *            parent
         *            /   \
         *       node       pr
         *                /   \
         *            prl       prr      (both prl and prr are not null)
         *            / \       / \
         *         prll prlr prrl prrr   (at least one is not null)
         */

        if (parent->right->balance == 0) {

            /**
             * right child of parent is balanced
             *
             * Before:
             *            parent
             *            /   \
             *       node       pr
             *                /   \
             *            prl       prr      (both prl and prr are not null)
             *            / \       / \
             *         prll prlr prrl prrr   (at least one in (prll, prlr) and one in (prrl, prrr) are not null)
             *
             * After rotate:
             *               pr
             *            /      \
             *         parent     prr
             *        /   \       /  \
             *    node    prl   prrl prrr
             *            / \
             *         prll prlr
             * 旋转后，处于不可完全平衡的状态，保持pr的平衡度不小于-1即可
             */

            avl_rotate_left(tree, parent);
            return;
        }

        if (parent->right->balance == 1) {

            /**
             * right child of parent is right heavy
             *
             * Before:
             *            parent
             *            /   \
             *       node       pr
             *                /   \
             *              prl    prr      (both prl and prr are not null)
             *                     / \
             *                  prrl prrr   (at least one in (prrl, prrr) are not null) 
             *
             * After rotate:
             *               pr
             *            /      \
             *         parent     prr
             *        /   \       /  \
             *    node    prl   prrl prrr
             * pr已经平衡，而旋转之前parent的parent的平衡度未知，需要进一步判断
             */
            
            avl_rotate_left(tree, parent);
            avl_post_delete(tree, parent->parent);
            return;
        }

        /**
         * right child of parent is left heavy
         *
         * Before:
         *            parent
         *            /   \
         *       node       pr
         *                /   \
         *            prl       prr      (both prl and prr are not null)
         *            / \
         *         prll prlr             (at least one in (prll, prlr) is not null)
         *
         * After rotate:
         *               prl
         *            /      \
         *         parent     pr
         *        /   \       /  \
         *    node    prll   prlr prr
         */

        avl_rotate_right(tree, parent->right);
        avl_rotate_left(tree, parent);
        avl_post_delete(tree, parent->parent);
        return;
    }

    /**
     * node is right child
     */

    parent->balance--;

    /**
     * parent is right heavy before
     */

    if (parent->balance == 0) {
        avl_post_delete(tree, parent);
        return;
    }

    /**
     * parent is balanced before
     */

    if (parent->balance == -1)
        return;

    /**
     * parent is left heavy before
     */

    if (parent->left->balance == 0) {

        /**
         * parent left child is balanced before
         */

        avl_rotate_right(tree, parent);
        return;
    }

    if (parent->left->balance == -1) {

        /**
         * parent left child is left heavy before
         */

        avl_rotate_right(tree, parent);
        avl_post_delete(tree, parent->parent);
        return;
    }

    /**
     * parent is left heavy before and 
     * parent left child is right heavy before
     */

    avl_rotate_left(tree, parent->left);
    avl_rotate_right(tree, parent);
    avl_post_delete(tree, parent->parent);
}

static struct avl_node *
avl_local_min(struct avl_node *node)
{
    while (node->left != NULL)
        node = node->left;

    return node;
}

#if 0
static struct avl_node *
avl_local_max(struct avl_node *node)
{
    while (node->right != NULL)
        node = node->right;

    return node;
}
#endif

static void
avl_delete_worker(struct avl_tree *tree, struct avl_node *node)
{
    struct avl_node *parent, *min;

    parent = node->parent;

    if (node->left == NULL && node->right == NULL) {
        // node is a leaf
        if (parent == NULL) {
            // node is the only element
            tree->root = NULL;
            return;
        }

        /**
         * node is the left child of parent
         */
        if (parent->left == node) {
            // delete node will cause parent right heavy
            parent->left = NULL;
            parent->balance++;

            if (parent->balance == 1)
                // parent is balance before
                return;

            if (parent->balance == 0) {
                // parent is left heavy before
                // grandperent should rebalance also
                avl_post_delete(tree, parent);
                return;
            }

            if (parent->right->balance == 0) {
                avl_rotate_left(tree, parent);
                return;
            }

            if (parent->right->balance == 1) {
                avl_rotate_left(tree, parent);
                avl_post_delete(tree, parent->parent);
                return;
            }

            avl_rotate_right(tree, parent->right);
            avl_rotate_left(tree, parent);
            avl_post_delete(tree, parent->parent);
            return;
        }

        if (parent->right == node) {
            parent->right = NULL;
            parent->balance--;

            if (parent->balance == -1)
                return;

            if (parent->balance == 0) {
                avl_post_delete(tree, parent);
                return;
            }

            if (parent->left->balance == 0) {
                avl_rotate_right(tree, parent);
                return;
            }

            if (parent->left->balance == -1) {
                avl_rotate_right(tree, parent);
                avl_post_delete(tree, parent->parent);
                return;
            }

            avl_rotate_left(tree, parent->left);
            avl_rotate_right(tree, parent);
            avl_post_delete(tree, parent->parent);
            return;
        }
    }

    if (node->left == NULL) {
        if (parent == NULL) {
            tree->root = node->right;
            node->right->parent = NULL;
            return;
        }

        node->right->parent = parent;

        if (parent->left == node)
            parent->left = node->right;

        else
            parent->right = node->right;

        avl_post_delete(tree, node->right);
        return;
    }

    if (node->right == NULL) {
        if (parent == NULL) {
            tree->root = node->left;
            node->left->parent = NULL;
            return;
        }

        node->left->parent = parent;

        if (parent->left == node)
            parent->left = node->left;

        else
            parent->right = node->left;

        avl_post_delete(tree, node->left);
        return;
    }

    min = avl_local_min(node->right);
    avl_delete_worker(tree, min);
    parent = node->parent;

    min->balance = node->balance;
    min->parent = parent;
    min->left = node->left;
    min->right = node->right;

    if (min->left != NULL)
        min->left->parent = min;

    if (min->right != NULL)
        min->right->parent = min;

    if (parent == NULL) {
        tree->root = min;
        return;
    }

    if (parent->left == node) {
        parent->left = min;
        return;
    }

    parent->right = min;
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
