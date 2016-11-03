#include "subscribe_thread.h"

#include <sys/types.h>
#include <sys/socket.h>

#include "simple_json.h"

#include <hiredis.h>

#define TITLE_MASTER        "p2p.retinue.master"
#define TITLE_VICE          "p2p.retinue.vice"


static redisContext *s_rds;
static lts_str_t STR_TRUE = lts_string("true");
static lts_str_t auth_k = lts_string("auth");
static lts_str_t port_restricted_k = lts_string("port_restricted");
static lts_str_t udp_port_k = lts_string("udp_port");

extern redisContext *redisGetConnection(void);
extern redisContext *redisCheckConnection(redisContext *rds);

int chan_sub[2]; // 订阅线程通道


static void handl_title_master(redisReply *reply)
{
    lts_pool_t *pool;
    sub_chanpack_t *data_ptr;
    lts_str_t input;
    lts_sjson_t output;
    lts_sjson_obj_node_t *obj_node;
    lts_sjson_kv_t *kv;
    lts_str_t *udp_port;

    pool = lts_create_pool(4096);
    if (NULL == pool) {
        return;
    }

    input = (lts_str_t){
        (uint8_t *)reply->element[2]->str,
        reply->element[2]->len,
    };
    output = lts_empty_sjson(pool);
    data_ptr = lts_palloc(pool, sizeof(sub_chanpack_t));

    // json解析及填充结构数据
    if (-1 == lts_sjson_decode(&input, &output)) {
        lts_destroy_pool(pool);
        return;
    }

    data_ptr->retinue_master = TRUE;
    data_ptr->pool = pool;

    obj_node = lts_sjson_get_obj_node(&output, &auth_k);
    if (! obj_node || (STRING_VALUE != obj_node->node_type)) {
        lts_destroy_pool(pool);
        return;
    }
    kv = CONTAINER_OF(obj_node, lts_sjson_kv_t, _obj_node);
    data_ptr->auth = lts_str_clone(&kv->val, pool);

    obj_node = lts_sjson_get_obj_node(&output, &port_restricted_k);
    if (! obj_node || (STRING_VALUE != obj_node->node_type)) {
        lts_destroy_pool(pool);
        return;
    }
    kv = CONTAINER_OF(obj_node, lts_sjson_kv_t, _obj_node);
    lts_str_lowercase(&kv->val);
    data_ptr->port_restricted = FALSE;
    if (0 == lts_str_compare(&STR_TRUE, &kv->val)) {
        data_ptr->port_restricted = TRUE;
    }

    obj_node = lts_sjson_get_obj_node(&output, &udp_port_k);
    if (! obj_node || (STRING_VALUE != obj_node->node_type)) {
        lts_destroy_pool(pool);
        return;
    }
    kv = CONTAINER_OF(obj_node, lts_sjson_kv_t, _obj_node);
    udp_port = lts_str_clone(&kv->val, pool);
    data_ptr->udp_port = atoi((char const *)udp_port->data);

    // 阻塞发送
    send(chan_sub[1], &data_ptr, sizeof(data_ptr), 0);

    return;
}


static void handl_title_vice(redisReply *reply)
{
    lts_pool_t *pool;
    sub_chanpack_t *data_ptr;
    lts_str_t input;
    lts_sjson_t output;
    lts_sjson_obj_node_t *obj_node;
    lts_sjson_kv_t *kv;
    lts_str_t *udp_port;

    pool = lts_create_pool(4096);
    if (NULL == pool) {
        return;
    }

    input = (lts_str_t){
        (uint8_t *)reply->element[2]->str,
        reply->element[2]->len,
    };
    output = lts_empty_sjson(pool);
    data_ptr = lts_palloc(pool, sizeof(sub_chanpack_t));

    // json解析及填充结构数据
    if (-1 == lts_sjson_decode(&input, &output)) {
        lts_destroy_pool(pool);
        return;
    }

    data_ptr->retinue_master = FALSE;
    data_ptr->pool = pool;

    obj_node = lts_sjson_get_obj_node(&output, &auth_k);
    if (! obj_node || (STRING_VALUE != obj_node->node_type)) {
        lts_destroy_pool(pool);
        return;
    }
    kv = CONTAINER_OF(obj_node, lts_sjson_kv_t, _obj_node);
    data_ptr->auth = lts_str_clone(&kv->val, pool);

    obj_node = lts_sjson_get_obj_node(&output, &udp_port_k);
    if (! obj_node || (STRING_VALUE != obj_node->node_type)) {
        lts_destroy_pool(pool);
        return;
    }
    kv = CONTAINER_OF(obj_node, lts_sjson_kv_t, _obj_node);
    udp_port = lts_str_clone(&kv->val, pool);
    data_ptr->udp_port = atoi((char const *)udp_port->data);

    // 阻塞发送
    send(chan_sub[1], &data_ptr, sizeof(data_ptr), 0);

    return;
}


void *subscribe_thread(void *arg)
{
    while (TRUE) {
        redisReply *reply;

        if (NULL == s_rds) {
            s_rds = redisGetConnection();
        }
        if (NULL == s_rds) {
            sleep(10);
            continue;
        }

        if (NULL == redisCheckConnection(s_rds)) {
            redisFree(s_rds);
            s_rds = NULL;
            sleep(10);
            continue;
        }

        reply = redisCommand(
            s_rds, "SUBSCRIBE "TITLE_MASTER" "TITLE_VICE
        );
        freeReplyObject(reply);
        redisGetReply(s_rds, (void **)&reply);
        freeReplyObject(reply);

        while(redisGetReply(s_rds, (void **)&reply) == REDIS_OK) {
            switch (reply->type) {
            case REDIS_REPLY_STATUS:
                break;
            case REDIS_REPLY_STRING:
                break;
            case REDIS_REPLY_ERROR:
                break;
            case REDIS_REPLY_NIL:
                break;
            case REDIS_REPLY_INTEGER:
                break;
            case REDIS_REPLY_ARRAY:
                // redis reply
                // 0: message
                // 1: 订阅的主题名称
                // 2: 推送的数据
                if (0 == strcmp(TITLE_MASTER, reply->element[1]->str)) {
                    handl_title_master(reply);
                } else if (0 == strcmp(TITLE_VICE, reply->element[1]->str)) {
                    handl_title_vice(reply);
                } else {
                    do {} while (0);
                }
                break;
            default:
                break;
            }
            freeReplyObject(reply);
        }
    }

    return NULL;
}
