#include <zlib.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>

#include <strings.h>

#include "latasia.h"
#include "logger.h"
#include "conf.h"
#include "protocol_sjsonb.h"
#include "rbt_timer.h"
#include "rbmap.h"

#include <hiredis.h>

#include "apps/p2p_fsm/logic_callback.h"

#define CONF_FILE           "conf/mod_app.conf"
#define __THIS_FILE__       "src/modules/mod_app_p2p_fsm.c"


extern uintptr_t time33(void *str, size_t len);
extern void *subscribe_thread(void *arg); // 订阅线程回调


typedef struct {
    lts_pool_t *pool; // 独立内存池
    lts_str_t *auth_token;
    lts_rbmap_node_t map_node;
    lts_socket_t *conn;
    dlist_t dlnode;
} tcp_session_t;
static dlist_t s_ts_cachelst;
static dlist_t s_ts_uselst;
static lts_rbmap_t s_ts_set; // 登录用户集合

static int __tcp_session_init(tcp_session_t *ts,
                              lts_socket_t *skt, lts_str_t *session)
{
    lts_pool_t *pool = lts_create_pool(4096);
    ts->pool = pool;
    ts->auth_token = lts_str_clone(session, pool);
    lts_rbmap_node_init(
        &ts->map_node, time33(session->data, session->len)
    );
    ts->conn = skt;

    return 0;
}
static void __tcp_session_release(tcp_session_t *ts)
{
    if (ts->pool) {
        lts_destroy_pool(ts->pool);
        ts->pool = NULL;
    }

    return;
}

static tcp_session_t *alloc_ts_instance(lts_socket_t *skt, lts_str_t *session)
{
    dlist_t *rslt;
    tcp_session_t *ts;

    if (dlist_empty(&s_ts_cachelst)) {
        return NULL;
    }

    rslt = s_ts_cachelst.mp_next;
    dlist_del(rslt);
    dlist_add_tail(&s_ts_uselst, rslt);

    ts = CONTAINER_OF(rslt, tcp_session_t, dlnode);
    __tcp_session_init(ts, skt, session);

    return ts;
}
static void free_ts_instance(tcp_session_t *ts)
{
    __tcp_session_release(ts);

    dlist_del(&ts->dlnode);
    dlist_add_tail(&s_ts_cachelst, &ts->dlnode);

    return;
}


static pthread_t s_sub_tid; // 订阅线程
static redisContext *s_rds; // redis连接
redisContext *redisCheckConnection(redisContext *rds)
{
    redisReply *reply;

    if (NULL == rds) {
        return NULL;
    }

    // 测试连接是否可用
    reply = redisCommand(rds, "auth %s", lts_p2p_fsm_conf.redis_auth);
    if (reply) {
        if (REDIS_REPLY_STATUS == reply->type) {
            lts_str_t status = {(uint8_t *)reply->str, reply->len};
            lts_str_t ok = lts_string("OK");

            if (0 == lts_str_compare(&ok, &status)) {
                freeReplyObject(reply);
                return rds;
            }
        }

        freeReplyObject(reply);
    }

    // check error in rds->err

    return NULL;
}
redisContext *redisGetConnection(void)
{
    redisContext *rds;

    // set up a new connection
    rds = redisConnect(lts_p2p_fsm_conf.redis_host,
                         lts_p2p_fsm_conf.redis_port);
    if (NULL == rds || rds->err) {
        // log
        (void)lts_write_logger(
            &lts_stderr_logger, LTS_LOG_ERROR,
            "%s:connect to redis failed, %s\n",
            STR_LOCATION, rds->errstr
        );

        if (rds) {
            redisFree(rds);
        }
        return NULL;
    }

    (void)redisCommand(rds, "auth %s", lts_p2p_fsm_conf.redis_auth);

    return rds;
}


// 加载模块配置
static int load_p2p_fsm_config(lts_conf_p2p_fsm_t *conf, lts_pool_t *pool)
{
    extern lts_conf_item_t *lts_conf_p2p_fsm_items[];

    off_t sz;
    uint8_t *addr;
    int rslt;
    lts_file_t lts_conf_file = {
        -1, {
            (uint8_t *)CONF_FILE, sizeof(CONF_FILE) - 1,
        },
    };

    if (-1 == load_conf_file(&lts_conf_file, &addr, &sz)) {
        return -1;
    }
    rslt = parse_conf(addr, sz, lts_conf_p2p_fsm_items, pool, conf);
    close_conf_file(&lts_conf_file, addr, sz);

    return rslt;
}


extern void make_simple_rsp(char *error_no, char *error_msg,
                            lts_buffer_t *sbuf, lts_pool_t *pool);

static int init_p2p_fsm_module(lts_module_t *module)
{
#define TS_SIZE     2
	lts_pool_t *pool;
    tcp_session_t *ats;

    // 创建模块内存池
    pool = lts_create_pool(MODULE_POOL_SIZE);
    if (NULL == pool) {
        // log
        return -1;
    }
    module->pool = pool;

	// 加载模块配置
    if (-1 == load_p2p_fsm_config(&lts_p2p_fsm_conf, pool)) {
        (void)lts_write_logger(
            &lts_stderr_logger, LTS_LOG_WARN,
            "%s:load app module config failed, using default\n",
            STR_LOCATION
        );
    }

    // redis连接
    s_rds = redisGetConnection();
    if (NULL == s_rds) {
        return -1;
    }

    // 订阅线程
    if (-1 == pthread_create(&s_sub_tid, NULL, &subscribe_thread, NULL)) {
        // log
        return -1;
    }

    // tcp session
    s_ts_set = lts_rbmap_entity;
    dlist_init(&s_ts_cachelst);
    dlist_init(&s_ts_uselst);

    ats = (tcp_session_t *)lts_palloc(pool, sizeof(tcp_session_t) * TS_SIZE);
    for (int i = 0; i < TS_SIZE; ++i) {
        dlist_add_tail(&s_ts_cachelst, &ats->dlnode);
        ++ats;
    }

    return 0;
#undef TS_SIZE
}


static void exit_p2p_fsm_module(lts_module_t *module)
{
    redisFree(s_rds);
    s_rds = NULL;

    return;
}


static void p2p_fsm_on_connected(lts_socket_t *s)
{
    return;
}


static void p2p_fsm_on_closing(lts_socket_t *s)
{
    return;
}


static void p2p_fsm_service(lts_socket_t *s)
{
    static lts_str_t itfc_k = lts_string("interface");
    static lts_str_t itfc_heartbeat_v = lts_string("heartbeat");
    static lts_str_t itfc_login_v = lts_string("login");
    static lts_str_t itfc_logout_v = lts_string("logout");
    static lts_str_t itfc_talkto_v = lts_string("talkto");
    static lts_str_t itfc_p2p_v = lts_string("p2p");
    static lts_str_t auth_k = lts_string("auth");
    static lts_str_t peer_auth_k = lts_string("peer_auth");
    static lts_str_t talk_msg_k = lts_string("message");

    lts_sjson_t *sjson;
    lts_buffer_t *rb = s->conn->rbuf;
    lts_buffer_t *sb = s->conn->sbuf;
    lts_pool_t *pool;

    // 用于json处理的暂时内存无法复用，每个请求使用新的内存池处理
    // 将来可以使用内存池重置解决
    pool = lts_create_pool(4096);

    sjson = lts_proto_sjsonb_decode(rb, pool);
    if (NULL == sjson) {
        // 非法请求
        connection_retimer(s, 0, TRUE);
        lts_destroy_pool(pool);
        return;
    }

    // 业务处理
    do {
    lts_sjson_obj_node_t *objnode; // 接口名
    lts_sjson_kv_t *kv_interface;

    objnode = lts_sjson_get_obj_node(sjson, &itfc_k);
    if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
        // 非法请求
        connection_retimer(s, 0, TRUE);
        break;
    }
    kv_interface = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

    // ======== 协议编码格式正确

    // 暂用if-else分发，将来使用hash或map
    if (0 == lts_str_compare(&kv_interface->val, &itfc_heartbeat_v)) {
        // 心跳
        connection_retimer(s, s->timeout + 600, FALSE); // 保持连接
        make_simple_rsp(E_SUCCESS, "success", s->conn->sbuf, pool);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_login_v)) {
        // 登录
        tcp_session_t *ts;
        lts_rbmap_node_t *ts_node;
        lts_sjson_kv_t *kv_auth;

        objnode = lts_sjson_get_obj_node(sjson, &auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing", sb, pool);
            break;
        }
        kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        ts_node = lts_rbmap_get(&s_ts_set,
                                time33(kv_auth->val.data, kv_auth->val.len));
        if (ts_node) {
            make_simple_rsp(E_EXIST, "existed", sb, pool);
            break;
        }

        ts = alloc_ts_instance(s, &kv_auth->val);
        if (NULL == ts) {
            // out of resource
            make_simple_rsp(E_SERVER_FAILED, "server failed", sb, pool);
            break;
        }
        (void)lts_rbmap_add(&s_ts_set, &ts->map_node);

        make_simple_rsp(E_SUCCESS, "success", sb, pool);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_talkto_v)) {
        // talkto
        tcp_session_t *peer_ts;
        lts_rbmap_node_t *ts_node;
        lts_sjson_kv_t *kv_auth, *kv_peer_auth, *kv_msg;
        lts_buffer_t *peer_sb;

        // 参数检查
        objnode = lts_sjson_get_obj_node(sjson, &auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing", sb, pool);
            break;
        }
        kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        objnode = lts_sjson_get_obj_node(sjson, &peer_auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing", sb, pool);
            break;
        }
        kv_peer_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        objnode = lts_sjson_get_obj_node(sjson, &talk_msg_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing", sb, pool);
            break;
        }
        kv_msg = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        // 登录检查
        ts_node = lts_rbmap_get(&s_ts_set,
                                time33(kv_auth->val.data, kv_auth->val.len));
        if (NULL == ts_node) {
            make_simple_rsp(E_NOT_EXIST, "not login", sb, pool);
            break;
        }
        ts_node = lts_rbmap_get(
            &s_ts_set, time33(kv_peer_auth->val.data, kv_peer_auth->val.len)
        );
        if (NULL == ts_node) {
            make_simple_rsp(E_NOT_EXIST, "peer not login", sb, pool);
            break;
        }
        peer_ts = CONTAINER_OF(ts_node, tcp_session_t, map_node);

        // 推送消息
        peer_sb = peer_ts->conn->conn->sbuf;
        peer_ts->conn->writable = 1;
        do {
            lts_sjson_t rsp = lts_empty_sjson(pool);
            lts_str_t *str_msg = lts_str_clone(&kv_msg->val, pool);

            lts_sjson_add_kv(&rsp, "errno", E_SUCCESS);
            lts_sjson_add_kv(&rsp, "errmsg", "success");
            lts_sjson_add_kv(&rsp, "message", (char const *)str_msg->data);
            assert(0 == lts_proto_sjsonb_encode(&rsp, peer_sb, pool));
        } while (0);
        make_simple_rsp(E_SUCCESS, "success", sb, pool);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_p2p_v)) {
        // p2p
        fprintf(stderr, "p2p\n");
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_logout_v)) {
        // 注销
        tcp_session_t *ts;
        lts_rbmap_node_t *ts_node;
        lts_sjson_kv_t *kv_auth;

        objnode = lts_sjson_get_obj_node(sjson, &auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing", sb, pool);
            break;
        }
        kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        ts_node = lts_rbmap_del(&s_ts_set,
                                time33(kv_auth->val.data, kv_auth->val.len));
        if (NULL == ts_node) {
            make_simple_rsp(E_NOT_EXIST, "not found", sb, pool);
            break;
        }

        ts = CONTAINER_OF(ts_node, tcp_session_t, map_node);
        free_ts_instance(ts);

        make_simple_rsp(E_SUCCESS, "success", sb, pool);
    } else {
        // 不支持的接口
        make_simple_rsp(E_NOT_SUPPORT, "unsupport interface", sb, pool);
    }
    } while (0);

    lts_destroy_pool(pool);
    return;
}


static void p2p_fsm_send_more(lts_socket_t *s)
{
    return;
}


static lts_app_module_itfc_t p2p_fsm_itfc = {
    &p2p_fsm_on_connected,
    &p2p_fsm_service,
    &p2p_fsm_send_more,
    &p2p_fsm_on_closing,
};

lts_module_t lts_app_p2p_fsm_module = {
    lts_string("lts_app_p2p_fsm_module"),
    LTS_APP_MODULE,
    &p2p_fsm_itfc,
    NULL,
    NULL,
    // interfaces
    NULL,
    &init_p2p_fsm_module,
    &exit_p2p_fsm_module,
    NULL,
};
