/**
 * latasia
 * Copyright (c) 2015 e7 <jackzxty@126.com>
 * */


#ifndef __LATASIA__SIMPLE_JSON_H__
#define __LATASIA__SIMPLE_JSON_H__


#include "list.h"
#include "rbtree.h"
#include "adv_string.h"
#include "mem_pool.h"


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// simple json表示键值叶子类型均为字符串
// 值类型
enum {
    STRING_VALUE = 1,
    LIST_VALUE,
    OBJ_VALUE,
};

typedef struct {
    int node_type;
    lts_str_t key;
    lts_rb_node_t tnode;
} lts_sjson_obj_node_t;

typedef struct {
    lts_rb_root_t val;
    lts_sjson_obj_node_t _obj_node; // 内部所用红黑树结点
    lstack_t _stk_node; // 内部所用栈结点
    lts_pool_t *pool; // 内存池
} lts_sjson_t;
#define lts_empty_sjson(pool) (lts_sjson_t){\
    RB_ROOT, (lts_sjson_obj_node_t){\
        OBJ_VALUE, lts_null_string, RB_NODE,\
    }, 0, pool\
}
#define lts_key_sjson(key, pool) (lts_sjson_t){\
    RB_ROOT, (lts_sjson_obj_node_t){\
        OBJ_VALUE, key, RB_NODE,\
    }, 0, pool\
}

typedef struct {
    list_t node;
    lts_str_t val;
} lts_sjson_li_node_t;

typedef struct {
    list_t *val;
    lts_sjson_obj_node_t _obj_node; // 内部所用红黑树结点
} lts_sjson_list_t;

typedef struct {
    lts_str_t val;
    lts_sjson_obj_node_t _obj_node; // 内部所用红黑树结点
} lts_sjson_kv_t;


// 构造sjson {{
// sjson对象嵌套
extern lts_sjson_t *lts_sjson_add_sjson(lts_sjson_t *sjson, char const *key);
extern lts_sjson_t *lts_sjson_add_sjson2(lts_sjson_t *sjson, lts_str_t *key);
// sjson对象增加键值对
extern lts_sjson_kv_t *lts_sjson_add_kv(lts_sjson_t *sjson,
                                        char const *key, char const *val);
extern lts_sjson_kv_t *lts_sjson_add_kv2(lts_sjson_t *sjson,
                                         lts_str_t *key, lts_str_t *val);
// }} 构造sjson


// 获取指定的值
extern lts_sjson_obj_node_t *lts_sjson_get_obj_node(lts_sjson_t *sjson,
                                                    lts_str_t *key);


// 计算编码后的大小
extern ssize_t lts_sjson_encode_size(lts_sjson_t *sjson);

// 编码
extern int lts_sjson_encode(lts_sjson_t *sjson, lts_str_t *output);

// 解码
extern int lts_sjson_decode(lts_str_t *src, lts_sjson_t *output);

// 弹出最小的元素
extern lts_sjson_obj_node_t *lts_sjson_pop_min(lts_sjson_t *obj);

// 遍历 {{
static inline lts_sjson_obj_node_t *lts_sjson_first(lts_sjson_t *obj)
{
    lts_rb_node_t *p = rb_first(&obj->val);

    return p ? CONTAINER_OF(p, lts_sjson_obj_node_t, tnode) : NULL;
}

static inline lts_sjson_obj_node_t *lts_sjson_last(lts_sjson_t *obj)
{
    lts_rb_node_t *p = rb_last(&obj->val);

    return p ? CONTAINER_OF(p, lts_sjson_obj_node_t, tnode) : NULL;
}

static inline lts_sjson_obj_node_t *lts_sjson_prev(lts_sjson_obj_node_t *node)
{
    lts_rb_node_t *p = rb_prev(&node->tnode);

    return p ? CONTAINER_OF(p, lts_sjson_obj_node_t, tnode) : NULL;
}

static inline lts_sjson_obj_node_t *lts_sjson_next(lts_sjson_obj_node_t *node)
{
    lts_rb_node_t *p = rb_next(&node->tnode);

    return p ? CONTAINER_OF(p, lts_sjson_obj_node_t, tnode) : NULL;
}
// }} 遍历

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // __LATASIA__SIMPLE_JSON_H__
