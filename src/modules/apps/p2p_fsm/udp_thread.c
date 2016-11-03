#include "udp_thread.h"

#include <sys/types.h>
#include <sys/socket.h>

#include "simple_json.h"

#define SOCKADDRLEN     sizeof(struct sockaddr)


int chan_udp[2]; // udp线程通道


void *udp_thread(void *arg)
{
    int lsn;
    uint8_t buf[512];
    struct sockaddr_in srvaddr;

    lsn = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == lsn) {
        return NULL;
    }

    memset(&srvaddr, 0, sizeof(srvaddr));
    srvaddr.sin_family = AF_INET;
    srvaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    srvaddr.sin_port = htons(1234);
    if (-1 == bind(lsn, (struct sockaddr *)&srvaddr, sizeof(srvaddr))) {
        return NULL;
    }

    while (TRUE) {
        ssize_t rcv_sz;
        uint8_t cli[SOCKADDRLEN] = {0};
        socklen_t cli_len = SOCKADDRLEN;
        udp_chanpack_t *data_ptr;
        struct sockaddr_in *addr_in;
        lts_pool_t *pool;
        lts_sjson_kv_t *kv_auth;

        rcv_sz = recvfrom(lsn, buf, sizeof(buf), 0,
                          (struct sockaddr *)cli, &cli_len);
        if (rcv_sz < 1) {
            continue;
        }

        addr_in = (struct sockaddr_in *)cli;

        pool = lts_create_pool(4096);
        if (NULL == pool) {
            continue;
        }

        // 解析sjson
        {
            static lts_str_t auth_k = lts_string("auth");

            lts_sjson_t output;
            lts_str_t str_buf;
            lts_sjson_obj_node_t *objnode;

            output = lts_empty_sjson(pool);
            lts_str_init(&str_buf, buf, rcv_sz);
            if (-1 == lts_sjson_decode(&str_buf, &output)) {
                lts_destroy_pool(pool);
                continue;
            }

            objnode = lts_sjson_get_obj_node(&output, &auth_k);
            if (NULL == objnode) {
                lts_destroy_pool(pool);
                continue;
            }
            kv_auth = CONTAINER_OF(objnode, lts_sjson_kv_t, _obj_node);
        }

        data_ptr = lts_palloc(pool, sizeof(udp_chanpack_t));
        data_ptr->pool = pool;
        data_ptr->peer_addr.ip = htonl(addr_in->sin_addr.s_addr);
        data_ptr->peer_addr.port = htons(addr_in->sin_port);
        data_ptr->auth = lts_str_clone(&kv_auth->val, pool);

        send(chan_udp[1], &data_ptr, sizeof(data_ptr), 0);
    }

    return NULL;
}
