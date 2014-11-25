
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_RBTREE_H_INCLUDED_
#define _NGX_RBTREE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_uint_t  ngx_rbtree_key_t;
typedef ngx_int_t   ngx_rbtree_key_int_t;


typedef struct ngx_rbtree_node_s  ngx_rbtree_node_t;

struct ngx_rbtree_node_s {
    ngx_rbtree_key_t       key;         /*数据*/
    ngx_rbtree_node_t     *left;        /*左子树*/
    ngx_rbtree_node_t     *right;       /*右子树*/
    ngx_rbtree_node_t     *parent;      /*父节点*/
    u_char                 color;        /*颜色*/
    u_char                 data;         /**/
};                                        /*红黑树的一个节点结构*/


typedef struct ngx_rbtree_s  ngx_rbtree_t;  /*红黑树类型*/

typedef void (*ngx_rbtree_insert_pt) (ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);      /*红黑树插入节点的函数指针*/

struct ngx_rbtree_s {
    ngx_rbtree_node_t     *root;            /*指向根节点*/
    ngx_rbtree_node_t     *sentinel;        /*哨兵节点*/
    ngx_rbtree_insert_pt   insert;          /*插入节点的函数指针*/
};                                             /*红黑树管理结构*/

/*红黑树初始化*/
#define ngx_rbtree_init(tree, s, i)                                           \
    ngx_rbtree_sentinel_init(s);                                              \
    (tree)->root = s;                                                         \
    (tree)->sentinel = s;                                                     \
    (tree)->insert = i


void ngx_rbtree_insert(ngx_thread_volatile ngx_rbtree_t *tree,
    ngx_rbtree_node_t *node);
void ngx_rbtree_delete(ngx_thread_volatile ngx_rbtree_t *tree,
    ngx_rbtree_node_t *node);
void ngx_rbtree_insert_value(ngx_rbtree_node_t *root, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel);
void ngx_rbtree_insert_timer_value(ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);


#define ngx_rbt_red(node)               ((node)->color = 1)             /*节点颜色设置为红色*/
#define ngx_rbt_black(node)             ((node)->color = 0)             /*节点颜色设置为黑色*/
#define ngx_rbt_is_red(node)            ((node)->color)                  /*‘断言‘是红色*/
#define ngx_rbt_is_black(node)          (!ngx_rbt_is_red(node))         /*‘断言‘是黑色*/
#define ngx_rbt_copy_color(n1, n2)      (n1->color = n2->color)         /*复制节点颜色*/


/* a sentinel must be black */

#define ngx_rbtree_sentinel_init(node)  ngx_rbt_black(node)             /*初始化哨兵节点*/


static ngx_inline ngx_rbtree_node_t *
ngx_rbtree_min(ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)   /*得到最近即将超时的事件*/
{
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}


#endif /* _NGX_RBTREE_H_INCLUDED_ */
