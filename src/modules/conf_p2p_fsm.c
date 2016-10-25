/**
 * latasia
 * Copyright (c) 2015 e7 <jackzxty@126.com>
 * */


#include "adv_string.h"
#include "mem_pool.h"
#include "conf.h"


// 默认配置
lts_conf_p2p_fsm_t lts_p2p_fsm_conf = {
    "127.0.0.1", 6379, ""
};


static void cb_host_match(void *c,
                          lts_str_t *k,
                          lts_str_t *v,
                          lts_pool_t *pool)
{
    char *host;

    host = (char *)lts_palloc(pool, v->len + 1);
    memcpy(host, v->data, v->len);
    host[v->len] = 0x00;

    lts_p2p_fsm_conf.redis_host = host;

    return;
}


static void cb_port_match(void *c,
                          lts_str_t *k,
                          lts_str_t *v,
                          lts_pool_t *pool)
{
    char *port;

    port = (char *)lts_palloc(pool, v->len + 1);
    memcpy(port, v->data, v->len);
    port[v->len] = 0x00;

    lts_p2p_fsm_conf.redis_port = atoi(port);

    return;
}


static void cb_auth_match(void *c,
                          lts_str_t *k,
                          lts_str_t *v,
                          lts_pool_t *pool)
{
    char *auth;

    auth = (char *)lts_palloc(pool, v->len + 1);
    memcpy(auth, v->data, v->len);
    auth[v->len] = 0x00;

    lts_p2p_fsm_conf.redis_auth = auth;

    return;
}


lts_conf_item_t *lts_conf_p2p_fsm_items[] = {
    &(lts_conf_item_t){lts_string("redis_host"), &cb_host_match},
    &(lts_conf_item_t){lts_string("redis_port"), &cb_port_match},
    &(lts_conf_item_t){lts_string("redis_auth"), &cb_auth_match},
    NULL,
};
