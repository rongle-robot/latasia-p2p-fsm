#include <zlib.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/epoll.h>

#include <strings.h>

#include "latasia.h"
#include "logger.h"
#include "conf.h"
#include "protocol_sjsonb.h"
#include "rbt_timer.h"
#include "rbmap.h"

#include <hiredis.h>

#include "apps/p2p_fsm/logic_callback.h"
#include "apps/p2p_fsm/subscribe_thread.h"
#include "apps/p2p_fsm/udp_thread.h"

#define CONF_FILE           "conf/mod_app.conf"
#define __THIS_FILE__       "src/modules/mod_app_p2p_fsm.c"


extern uintptr_t time33(void *str, size_t len);


enum {
    FSM_IDLE = 0, // 空闲态
    FSM_WAIT_RETINUE_MASTER,
    FSM_WAIT_RETINUE_VICE,
};
enum {
    NAT_UNKNOWN = 0, // 未知NAT类型
    NAT_SYMMETIC, // 对称型
    NAT_PORT_RESTRICTED_CONE, // 端口受限锥型
    NAT_RESTRICTED_CONE, // IP受限锥型
    NAT_FULL_CONE, // 全锥型
};
typedef struct {
    uint32_t fsm_stat; // p2p穿透状态机
    lts_pool_t *pool; // 独立内存池
    lts_str_t *auth_token;
    int port_restricted; // 是否端口受限
    int pre_udp_port;
    int nat_type; // nat类型
    udp_hole_t udp_hole; // udp穿透信息
    lts_rbmap_node_t map_node;
    lts_socket_t *conn;
    dlist_t dlnode;
} tcp_session_t;
static dlist_t s_ts_cachelst;
static dlist_t s_ts_uselst;
static lts_rbmap_t s_ts_set; // 登录用户集合


#define ts_change_skt(ts, skt)      do {\
    (ts)->conn = skt;\
    (skt)->app_ctx = ts; /* 底层保存的上下文数据用于反查 */\
} while (0)

static int __tcp_session_init(tcp_session_t *ts,
                              lts_socket_t *skt, lts_str_t *session)
{
    lts_pool_t *pool = lts_create_pool(4096);

    ts->fsm_stat = FSM_IDLE;
    ts->pool = pool;
    ts->auth_token = lts_str_clone(session, pool);
    ts->port_restricted = FALSE;
    ts->pre_udp_port = 0;
    ts->nat_type = NAT_UNKNOWN;
    memset(&ts->udp_hole, 0, sizeof(ts->udp_hole));
    lts_rbmap_node_init(
        &ts->map_node, time33(session->data, session->len)
    );
    ts_change_skt(ts, skt);

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
static tcp_session_t *find_ts_by_auth(lts_str_t *auth)
{
    lts_rbmap_node_t *ts_node;

    ts_node = lts_rbmap_get(&s_ts_set, time33(auth->data, auth->len));
    if (NULL == ts_node) {
        return NULL;
    }

    return CONTAINER_OF(ts_node, tcp_session_t, map_node);
}


static pthread_t s_sub_tid; // 订阅线程
static pthread_t s_udp_tid; // udp线程
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
            "%s:connect to redis failed, %s\n", STR_LOCATION, rds->errstr
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


static void on_channel_sub(lts_socket_t *cs)
{
    ssize_t rcv_sz;
    sub_chanpack_t *data_ptr;
    tcp_session_t *ts;

    rcv_sz = recv(cs->fd, &data_ptr, sizeof(data_ptr), 0);
    if (-1 == rcv_sz) {
        // ignore errno
        cs->readable = 0;
        return;
    }

    // 之后任何返回记得调用lts_destroy_pool(data_ptr->pool)，而且销
    // 毁内存池之后不得再访问data_ptr变量

    ts = find_ts_by_auth(data_ptr->auth);
    if (NULL == ts) {
        (void)lts_write_logger(
            &lts_file_logger, LTS_LOG_WARN,
            "%s:invalid session '%s', ignore retinue report\n",
            STR_LOCATION, data_ptr->auth->data
        );

        lts_destroy_pool(data_ptr->pool);
        return;
    }

    // 状态机
    switch (ts->fsm_stat) {
    case FSM_IDLE:
        if (data_ptr->retinue_master) {
            ts->fsm_stat = FSM_WAIT_RETINUE_VICE;
            ts->port_restricted = data_ptr->port_restricted;
            ts->pre_udp_port = data_ptr->udp_port;
        } else {
            ts->fsm_stat = FSM_WAIT_RETINUE_MASTER;
            ts->pre_udp_port = data_ptr->udp_port;
        }

        break;

    case FSM_WAIT_RETINUE_MASTER:
        if (! data_ptr->retinue_master) {
            break;
        }

        if (ts->pre_udp_port != data_ptr->udp_port) {
            ts->nat_type = NAT_SYMMETIC;
        } else if (ts->port_restricted) {
            ts->nat_type = NAT_PORT_RESTRICTED_CONE;
        } else {
            ts->nat_type = NAT_RESTRICTED_CONE;
        }

        ts->fsm_stat = FSM_IDLE;
        ts->pre_udp_port = 0;

        break;

    case FSM_WAIT_RETINUE_VICE:
        if (data_ptr->retinue_master) {
            break;
        }

        if (ts->pre_udp_port != data_ptr->udp_port) {
            ts->nat_type = NAT_SYMMETIC;
        } else if (ts->port_restricted) {
            ts->nat_type = NAT_PORT_RESTRICTED_CONE;
        } else {
            ts->nat_type = NAT_RESTRICTED_CONE;
        }

        ts->fsm_stat = FSM_IDLE;
        ts->pre_udp_port = 0;

        break;

    default:
        break;
    }

    lts_destroy_pool(data_ptr->pool);

    return;
}


static void on_channel_udp(lts_socket_t *cs)
{
    ssize_t rcv_sz;
    udp_chanpack_t *data_ptr;
    tcp_session_t *ts;

    rcv_sz = recv(cs->fd, &data_ptr, sizeof(data_ptr), 0);
    if (-1 == rcv_sz) {
        // ignore error
        cs->readable = 0;
        return;
    }

    // 之后任何返回记得调用lts_destroy_pool(data_ptr->pool)，而且销
    // 毁内存池之后不得再访问data_ptr变量

    ts = find_ts_by_auth(data_ptr->auth);
    if (ts) {
        // 更新穿透信息
        ts->udp_hole.ip = data_ptr->peer_addr.ip;
        ts->udp_hole.port = data_ptr->peer_addr.port;
    } else {
        (void)lts_write_logger(
            &lts_file_logger, LTS_LOG_WARN,
            "%s:invalid session '%s', ignore udp heartbeat\n",
            STR_LOCATION, data_ptr->auth->data
        );
    }

    lts_destroy_pool(data_ptr->pool);
}


static int init_p2p_fsm_module(lts_module_t *module)
{
#define TS_SIZE     65535
	lts_pool_t *pool;
    tcp_session_t *ats;
    lts_socket_t *skt_chan_sub, *skt_chan_udp;

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

    // 创建通信管线
    if (-1 == socketpair(AF_UNIX, SOCK_STREAM, 0, chan_sub)) {
        // log
        return -1;
    }
    if (lts_set_nonblock(chan_sub[0])) { // 0号用于主线程
        // log
    }
    skt_chan_sub = lts_alloc_socket();
    skt_chan_sub->fd = chan_sub[0];
    skt_chan_sub->ev_mask = EPOLLIN | EPOLLET;
    skt_chan_sub->conn = NULL;
    skt_chan_sub->do_read = &on_channel_sub;
    skt_chan_sub->do_write = NULL;
    lts_timer_node_init(&skt_chan_sub->timer_node, 0, NULL);
    (lts_event_itfc->event_add)(skt_chan_sub); // 加入事件监控

    if (-1 == socketpair(AF_UNIX, SOCK_STREAM, 0, chan_udp)) {
        // log
        return -1;
    }
    if (lts_set_nonblock(chan_udp[0])) { // 0号用于主线程
        // log
    }
    skt_chan_udp = lts_alloc_socket();
    skt_chan_udp->fd = chan_udp[0];
    skt_chan_udp->ev_mask = EPOLLIN | EPOLLET;
    skt_chan_udp->conn = NULL;
    skt_chan_udp->do_read = &on_channel_udp;
    skt_chan_udp->do_write = NULL;
    lts_timer_node_init(&skt_chan_udp->timer_node, 0, NULL);
    (lts_event_itfc->event_add)(skt_chan_udp); // 加入事件监控

    // 订阅线程
    if (-1 == pthread_create(&s_sub_tid, NULL, &subscribe_thread, NULL)) {
        // log
        return -1;
    }

    // udp线程
    if (-1 == pthread_create(&s_udp_tid, NULL, &udp_thread, NULL)) {
        // log
        return -1;
    }

    // tcp session
    s_ts_set = lts_rbmap_entity;
    dlist_init(&s_ts_cachelst);
    dlist_init(&s_ts_uselst);

    ats = (tcp_session_t *)lts_palloc(pool, sizeof(tcp_session_t) * TS_SIZE);
    if (NULL == ats) {
        (void)lts_write_logger(&lts_file_logger, LTS_LOG_EMERGE,
                               "%s:out of memory\n",STR_LOCATION);
        return -1;
    }

    for (int i = 0; i < TS_SIZE; ++i) {
        dlist_add_tail(&s_ts_cachelst, &ats->dlnode);
        ++ats;
    }

    return 0;
#undef TS_SIZE
}


static void exit_p2p_fsm_module(lts_module_t *module)
{
    (void)close(chan_sub[0]);
    (void)close(chan_sub[1]);

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
    tcp_session_t *ts;

    ts = (tcp_session_t *)s->app_ctx;
    if (ts && (ts->conn == s)) {
        ts->conn = NULL;
        s->app_ctx = NULL;
    }

    return;
}


static void p2p_fsm_service(lts_socket_t *s)
{
    static lts_str_t itfc_k = lts_string("interface");
    static lts_str_t itfc_heartbeat_v = lts_string("heartbeat");
    static lts_str_t itfc_login_v = lts_string("login");
    static lts_str_t itfc_logout_v = lts_string("logout");
    static lts_str_t itfc_talkto_v = lts_string("talkto");
    static lts_str_t itfc_getlinkinfo_v = lts_string("getlinkinfo");
    static lts_str_t auth_k = lts_string("auth");
    static lts_str_t peerauth_k = lts_string("peerauth");
    static lts_str_t talk_msg_k = lts_string("message");

    lts_sjson_t *sjson;
    lts_buffer_t *rbuf = s->conn->rbuf;
    lts_buffer_t *sbuf;
    lts_pool_t *pool;

    // 用于json处理的暂时内存无法复用，每个请求使用新的内存池处理
    // 将来可以使用内存池重置解决
    pool = lts_create_pool(4096);

    sjson = lts_proto_sjsonb_decode(rbuf, pool);
    if (NULL == sjson) {
        // 非法请求
        uintptr_t expire_time = 0;

        while (-1 == lts_timer_reset(&lts_timer_heap,
                                     &s->timer_node,
                                     expire_time++));
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
        uintptr_t expire_time = 0;

        while (-1 == lts_timer_reset(&lts_timer_heap,
                                     &s->timer_node,
                                     expire_time++));
        lts_destroy_pool(pool);
        break;
    }
    kv_interface = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

    // ======== 协议编码格式正确

    // 暂用if-else分发，将来使用hash或map
    if (0 == lts_str_compare(&kv_interface->val, &itfc_heartbeat_v)) {
        // 心跳
        uintptr_t expire_time = s->timer_node.mapnode.key + 600;

        // 保持连接
        while (-1 == lts_timer_reset(&lts_timer_heap,
                                     &s->timer_node,
                                     expire_time++));
        make_simple_rsp(E_SUCCESS, "success", s->conn->sbuf, pool);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_login_v)) {
        // 登录
        tcp_session_t *ts;
        lts_sjson_kv_t *kv_auth;

        objnode = lts_sjson_get_obj_node(sjson, &auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing",
                            s->conn->sbuf, pool);
            break;
        }
        kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        ts = find_ts_by_auth(&kv_auth->val);
        if (ts) {
            if (NULL == ts->conn) {
                ts_change_skt(ts, s);
            } else if (s != ts->conn) {
                // 踢掉老连接
                if (ts->conn) {
                    uintptr_t expire_time = 0;

                    while (-1 == lts_timer_reset(&lts_timer_heap,
                                                 &ts->conn->timer_node,
                                                 expire_time++));
                }
                ts_change_skt(ts, s);
            } else {
            }
        } else {
            ts = alloc_ts_instance(s, &kv_auth->val);
            if (NULL == ts) {
                // out of resource
                make_simple_rsp(E_SERVER_FAILED, "server failed",
                                s->conn->sbuf, pool);
                break;
            }
            (void)lts_rbmap_add(&s_ts_set, &ts->map_node);
        }

        // 返回retinue信息并修改状态
        sbuf = ts->conn->conn->sbuf;
        do {
            lts_sjson_t rsp = lts_empty_sjson(pool);

            lts_sjson_add_kv(&rsp, K_ERRNO, E_SUCCESS);
            lts_sjson_add_kv(&rsp, K_ERRMSG, "success");
            lts_sjson_add_kv(&rsp, "retinue_master",
                             lts_p2p_fsm_conf.retinue_master);
            lts_sjson_add_kv(&rsp, "retinue_vice",
                             lts_p2p_fsm_conf.retinue_vice);
            (void)lts_proto_sjsonb_encode(&rsp, sbuf, pool);
        } while (0);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_talkto_v)) {
        // talkto
        tcp_session_t *peer_ts, *ts;
        lts_sjson_kv_t *kv_auth, *kv_peerauth, *kv_msg;
        lts_buffer_t *peer_sb;

        // 参数检查
        objnode = lts_sjson_get_obj_node(sjson, &auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing",
                            s->conn->sbuf, pool);
            break;
        }
        kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        objnode = lts_sjson_get_obj_node(sjson, &peerauth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing",
                            s->conn->sbuf, pool);
            break;
        }
        kv_peerauth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        objnode = lts_sjson_get_obj_node(sjson, &talk_msg_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing",
                            s->conn->sbuf, pool);
            break;
        }
        kv_msg = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        // 登录检查
        ts = find_ts_by_auth(&kv_auth->val);
        if (NULL == ts) {
            make_simple_rsp(E_NOT_EXIST, "not login",
                            s->conn->sbuf, pool);
            break;
        }
        peer_ts = find_ts_by_auth(&kv_peerauth->val);
        if ((NULL == peer_ts) || (NULL == peer_ts->conn)) {
            make_simple_rsp(E_NOT_EXIST, "peer not login",
                            s->conn->sbuf, pool);
            break;
        }

        if (s != ts->conn) {
            make_simple_rsp(E_INVALID_ARG, "invalid session",
                            s->conn->sbuf, pool);
            break;
        }

        // 推送消息
        sbuf = s->conn->sbuf;
        peer_sb = peer_ts->conn->conn->sbuf;
        do {
            lts_sjson_t rsp = lts_empty_sjson(pool);
            lts_str_t *str_msg = lts_str_clone(&kv_msg->val, pool);

            lts_sjson_add_kv(&rsp, K_ERRNO, E_SUCCESS);
            lts_sjson_add_kv(&rsp, K_ERRMSG, "success");
            lts_sjson_add_kv(&rsp, "message", (char const *)str_msg->data);
            (void)lts_proto_sjsonb_encode(&rsp, peer_sb, pool);
            lts_soft_event(peer_ts->conn, 1);
        } while (0);
        make_simple_rsp(E_SUCCESS, "success", sbuf, pool);
    } else if (0 == lts_str_compare(&kv_interface->val,
                                    &itfc_getlinkinfo_v)) {
        // getlinkinfo
        tcp_session_t *ts, *peer_ts;
        lts_sjson_kv_t *kv_auth, *kv_peerauth;

        // 参数检查
        objnode = lts_sjson_get_obj_node(sjson, &auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing",
                            s->conn->sbuf, pool);
            break;
        }
        kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        objnode = lts_sjson_get_obj_node(sjson, &peerauth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing",
                            s->conn->sbuf, pool);
            break;
        }
        kv_peerauth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        if (0 == lts_str_compare(&kv_auth->val, &kv_peerauth->val)) {
            make_simple_rsp(E_INVALID_ARG, "auth equals to peerauth",
                            s->conn->sbuf, pool);
            break;
        }

        // 登录检查
        ts = find_ts_by_auth(&kv_auth->val);
        if (NULL == ts) {
            make_simple_rsp(E_NOT_EXIST, "not login",
                            s->conn->sbuf, pool);
            break;
        }
        peer_ts = find_ts_by_auth(&kv_peerauth->val);
        if ((NULL == peer_ts) || (NULL == peer_ts->conn)) {
            make_simple_rsp(E_NOT_EXIST, "peer not login",
                            s->conn->sbuf, pool);
            break;
        }

        if (s != ts->conn) {
            make_simple_rsp(E_INVALID_ARG, "invalid session",
                            s->conn->sbuf, pool);
            break;
        }

        // 返回链路信息
        sbuf = ts->conn->conn->sbuf;
        do {
            lts_sjson_t rsp = lts_empty_sjson(pool);

            lts_sjson_add_kv(&rsp, K_ERRNO, E_SUCCESS);
            lts_sjson_add_kv(&rsp, K_ERRMSG, "success");
            lts_sjson_add_kv(&rsp, "ip",
                             lts_uint322cstr(ts->udp_hole.ip));
            lts_sjson_add_kv(&rsp, "port",
                             lts_uint322cstr(ts->udp_hole.port));
            switch (ts->nat_type) {
            case NAT_SYMMETIC:
                lts_sjson_add_kv(&rsp, "nattype", "symmetic");
                break;
            case NAT_PORT_RESTRICTED_CONE:
                lts_sjson_add_kv(&rsp, "nattype", "portrestricted");
                break;
            case NAT_RESTRICTED_CONE:
                lts_sjson_add_kv(&rsp, "nattype", "restricted");
                break;
            case NAT_FULL_CONE:
                lts_sjson_add_kv(&rsp, "nattype", "full");
                break;
            default:
                lts_sjson_add_kv(&rsp, "nattype", "unknown");
                break;
            }
            lts_sjson_add_kv(&rsp, "peerip",
                             lts_uint322cstr(peer_ts->udp_hole.ip));
            lts_sjson_add_kv(&rsp, "peerport",
                             lts_uint322cstr(peer_ts->udp_hole.port));
            switch (peer_ts->nat_type) {
            case NAT_SYMMETIC:
                lts_sjson_add_kv(&rsp, "peernattype", "symmetic");
                break;
            case NAT_PORT_RESTRICTED_CONE:
                lts_sjson_add_kv(&rsp, "peernattype", "portrestricted");
                break;
            case NAT_RESTRICTED_CONE:
                lts_sjson_add_kv(&rsp, "peernattype", "restricted");
                break;
            case NAT_FULL_CONE:
                lts_sjson_add_kv(&rsp, "peernattype", "full");
                break;
            default:
                lts_sjson_add_kv(&rsp, "peernattype", "unknown");
                break;
            }

            (void)lts_proto_sjsonb_encode(&rsp, sbuf, pool);
        } while (0);
    } else if (0 == lts_str_compare(&kv_interface->val, &itfc_logout_v)) {
        // 注销
        tcp_session_t *ts;
        lts_rbmap_node_t *ts_node;
        lts_sjson_kv_t *kv_auth;

        objnode = lts_sjson_get_obj_node(sjson, &auth_k);
        if ((NULL == objnode) || (STRING_VALUE != objnode->node_type)) {
            make_simple_rsp(E_ARG_MISS, "argument missing",
                            s->conn->sbuf, pool);
            break;
        }
        kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);

        ts_node = lts_rbmap_get(&s_ts_set,
                                time33(kv_auth->val.data, kv_auth->val.len));
        if (NULL == ts_node) {
            make_simple_rsp(E_NOT_EXIST, "not found",
                            s->conn->sbuf, pool);
            break;
        }
        ts = CONTAINER_OF(ts_node, tcp_session_t, map_node);

        if (s != ts->conn) {
            make_simple_rsp(E_INVALID_ARG, "invalid session",
                            s->conn->sbuf, pool);
            break;
        }

        lts_rbmap_safe_del(&s_ts_set, &ts->map_node);
        free_ts_instance(ts);

        (void)lts_write_logger(
            &lts_stderr_logger, LTS_LOG_INFO,
            "session '%s' logout\n", lts_str_clone(&kv_auth->val, pool)->data
        );

        make_simple_rsp(E_SUCCESS, "success", s->conn->sbuf, pool);
    } else {
        // 不支持的接口
        make_simple_rsp(E_NOT_SUPPORT, "unsupport interface",
                        s->conn->sbuf, pool);
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
