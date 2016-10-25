#include <zlib.h>
#include <arpa/inet.h>
#include <dirent.h>

#include <strings.h>

#include "latasia.h"
#include "rbtree.h"
#include "logger.h"
#include "conf.h"

#define __THIS_FILE__       "src/modules/mod_app_echo.c"


static int init_echo_module(lts_module_t *module)
{
    return 0;
}


static void exit_echo_module(lts_module_t *module)
{
    return;
}


static void echo_on_connected(lts_socket_t *s)
{
    return;
}


static void echo_service(lts_socket_t *s)
{
    lts_buffer_t *rb = s->conn->rbuf;
    lts_buffer_t *sb = s->conn->sbuf;

    // 清空原发送缓冲
    lts_buffer_clear(sb);

    // 交换
    s->conn->rbuf = sb;
    s->conn->sbuf = rb;

    return;
}


static void echo_send_more(lts_socket_t *s)
{
    return;
}


static lts_app_module_itfc_t echo_itfc = {
    &echo_on_connected,
    &echo_service,
    &echo_send_more,
    NULL,
};

lts_module_t lts_app_echo_module = {
    lts_string("lts_app_echo_module"),
    LTS_APP_MODULE,
    &echo_itfc,
    NULL,
    NULL,
    // interfaces
    NULL,
    &init_echo_module,
    &exit_echo_module,
    NULL,
};
