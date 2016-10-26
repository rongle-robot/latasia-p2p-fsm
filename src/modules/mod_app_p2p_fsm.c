#include <zlib.h>
#include <arpa/inet.h>
#include <dirent.h>

#include <strings.h>

#include "latasia.h"
#include "logger.h"
#include "conf.h"
#include "protocol_sjsonb.h"
#include "rbt_timer.h"

#include <hiredis.h>

#define CONF_FILE           "conf/mod_app.conf"
#define __THIS_FILE__       "src/modules/mod_app_p2p_fsm.c"


// 强制超时让连接关闭
#define imply_closing(s) do {\
    lts_timer_heap_del(&lts_timer_heap, s);\
    (s)->timeout = 0;\
    lts_timer_heap_add(&lts_timer_heap, s);\
} while (0)


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
	lts_pool_t *pool;

    // 创建模块内存池
    pool = lts_create_pool(MODULE_POOL_SIZE);
    if (NULL == pool) {
        // log
        return -1;
    }
    module->pool = pool;

	// 加载模块配置
    if (-1 == load_p2p_fsm_config(&lts_asyn_conf, pool)) {
        (void)lts_write_logger(
            &lts_stderr_logger, LTS_LOG_WARN,
            "%s:load app module config failed, using default\n",
            STR_LOCATION
        );
    }

    return 0;
}


static void exit_p2p_fsm_module(lts_module_t *module)
{
    return;
}


static void p2p_fsm_on_connected(lts_socket_t *s)
{
    redisContext *rds;

    rds = redisConnect(lts_p2p_fsm_conf.redis_host,
                       lts_p2p_fsm_conf.redis_port);
    if (rds->err) {
        // log
		imply_closing(s);

        redisFree(rds);
        return;
    }

    redisFree(rds);
    return;
}


static void p2p_fsm_on_closing(lts_socket_t *s)
{
    redisContext *rds;

    rds = redisConnect(lts_p2p_fsm_conf.redis_host,
                       lts_p2p_fsm_conf.redis_port);
    if (rds->err) {
        // log

        redisFree(rds);
        return;
    }

    redisFree(rds);
    return;
}


static void p2p_fsm_service(lts_socket_t *s)
{
    lts_sjson_t *sjson;
    lts_buffer_t *rb = s->conn->rbuf;
    lts_buffer_t *sb = s->conn->sbuf;
    lts_pool_t *pool;

    // 用于json处理的内存无法复用，每个请求使用新的内存池处理
    // 将来可以使用池中池解决
    pool = lts_create_pool(4096);

    sjson = lts_proto_sjsonb_decode(rb, pool);
    if (NULL == sjson) {
        lts_destroy_pool(pool);
        return;
    }

    lts_str_t key = lts_string("lala");
    lts_str_t val = lts_string("tiannalu");
    lts_sjson_add_kv(sjson, &key, &val);
    fprintf(stderr, "get json\n");
    fprintf(stderr, "%d\n", lts_proto_sjsonb_encode(sjson, sb));

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
