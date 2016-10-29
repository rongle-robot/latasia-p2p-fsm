#include <zlib.h>
#include <arpa/inet.h>
#include <dirent.h>

#include <strings.h>

#include "latasia.h"
#include "logger.h"
#include "conf.h"
#include "protocol_sjsonb.h"
#include "rbt_timer.h"
#include "hashset.h"

#include <hiredis.h>

#define CONF_FILE           "conf/mod_app.conf"
#define __THIS_FILE__       "src/modules/mod_app_p2p_fsm.c"


// 重置定时器
#define connection_retimer(s, v) do {\
    lts_timer_heap_del(&lts_timer_heap, s);\
    (s)->timeout = v;\
    lts_timer_heap_add(&lts_timer_heap, s);\
} while (0)


typedef struct {
    lts_pool_t *pool;
    lts_str_t *auth_token;
    lts_rb_node_t rbnode;
    lts_socket_t *conn;
    dlist_t dlnode;
} tcp_session_t;
static dlist_t s_ts_cachelst;
static dlist_t s_ts_uselst;
static lts_hashset_t s_ts_set; // 登录用户集合

static int __tcp_session_init(tcp_session_t *ts, lts_str_t *session)
{
    lts_pool_t *pool = lts_create_pool(4096);

    ts->pool = pool;
    ts->auth_token = lts_str_clone(session, pool);
    ts->rbnode = RB_NODE;
    RB_CLEAR_NODE(&ts->rbnode);

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

static tcp_session_t *alloc_ts_instance(lts_str_t *session)
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
    __tcp_session_init(ts, session);

    return ts;
}
static void free_ts_instance(tcp_session_t *ts)
{
    __tcp_session_release(ts);

    dlist_del(&ts->dlnode);
    dlist_add_tail(&s_ts_cachelst, &ts->dlnode);

    return;
}


// hash
static uint32_t __time33(char const *str, ssize_t len)
{
    uint32_t hash = 0;

    for (size_t i = 0; i < len; ++i) {
        hash = hash *33 + (uint32_t)str[i];
    }

    return hash;
}
static uint32_t tcp_session_hash(void *arg)
{
    tcp_session_t *ts = (tcp_session_t *)arg;
    return __time33((char const *)ts->auth_token->data, ts->auth_token->len);
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

    // 初始化
    s_ts_set = lts_hashset_entity(tcp_session_t, rbnode, &tcp_session_hash);
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
    return;
}


static void p2p_fsm_on_connected(lts_socket_t *s)
{
    redisContext *rds;
    lts_pool_t *pool;
    lts_sjson_t sjson;
    struct sockaddr_in *si = (struct sockaddr_in *)s->peer_addr;
    lts_str_t output;

    rds = redisConnect(lts_p2p_fsm_conf.redis_host,
                       lts_p2p_fsm_conf.redis_port);
    if (NULL == rds || rds->err) {
        // log
        fprintf(stderr, "no redis\n");

        if (rds) {
            redisFree(rds);
        }

        return;
    }

    pool = lts_create_pool(4096);
    sjson = lts_empty_sjson(pool);

    lts_sjson_add_kv(&sjson, "tcp_port", lts_uint322cstr(ntohs(si->sin_port)));
    lts_sjson_encode(&sjson, &output);

    (redisReply*)redisCommand(rds, "auth %s", lts_p2p_fsm_conf.redis_auth);
    (redisReply*)redisCommand(rds, "set %s %s",
                              lts_inet_ntoa(si->sin_addr), output.data);

    lts_destroy_pool(pool);
    redisFree(rds);
    return;
}


static void p2p_fsm_on_closing(lts_socket_t *s)
{
    redisContext *rds;
    struct sockaddr_in *si = (struct sockaddr_in *)s->peer_addr;

    rds = redisConnect(lts_p2p_fsm_conf.redis_host,
                       lts_p2p_fsm_conf.redis_port);
    if (NULL == rds || rds->err) {
        if (rds) {
            redisFree(rds);
        }

        return;
    }

    (redisReply*)redisCommand(rds, "auth %s", lts_p2p_fsm_conf.redis_auth);
    (redisReply*)redisCommand(rds, "del %s", lts_inet_ntoa(si->sin_addr));

    redisFree(rds);
    return;
}


static void p2p_fsm_service(lts_socket_t *s)
{
    static lts_str_t itfc_k = lts_string("interface");
    static lts_str_t itfc_heartbeat_v = lts_string("heartbeat");
    static lts_str_t itfc_login_v = lts_string("login");
    static lts_str_t itfc_logout_v = lts_string("logout");

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
        connection_retimer(s, 0);
        lts_destroy_pool(pool);
        return;
    }

    // 业务处理
    do {
    lts_sjson_obj_node_t *interface; // 接口名
    lts_sjson_kv_t *kv_interface;

    interface = lts_sjson_get_obj_node(sjson, &itfc_k);
    if ((NULL == interface) || (STRING_VALUE != interface->node_type)) {
        // 非法请求
        connection_retimer(s, 0);
        lts_destroy_pool(pool);
        return;
    }
    kv_interface = CONTAINER_OF(interface, lts_sjson_kv_t, _obj_node);

    // 暂用if-else分发，将来使用hash或map
    if (0 == lts_str_compare(&kv_interface->val, &itfc_heartbeat_v)) {
        // 心跳
        connection_retimer(s, s->timeout + 600); // 保持连接
        lts_proto_sjsonb_encode(sjson, sb);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_login_v)) {
        // 登录
        lts_str_t auth = lts_string("FjgGaashga");
        tcp_session_t *ts;

        ts = alloc_ts_instance(&auth);
        assert(0 == lts_hashset_add(&s_ts_set, ts));
        assert(ts == lts_hashset_get(&s_ts_set, ts));
        lts_hashset_del(&s_ts_set, ts);
        free_ts_instance(ts);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_logout_v)) {
        // 注销
    } else {
        // 不支持的接口
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
