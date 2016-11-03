#ifndef __APP_UDP_THREAD_H__
#define __APP_UDP_THREAD_H__


#include "mem_pool.h"
#include "adv_string.h"
#include "socket.h"


typedef struct {
    uint32_t ip;
    uint16_t port;
} udp_hole_t;

typedef struct {
    lts_pool_t *pool;
    udp_hole_t peer_addr;
    lts_str_t *auth;
} udp_chanpack_t;


extern int chan_udp[2]; // udp线程通道
extern void *udp_thread(void *arg); // udp线程回调
#endif // __APP_UDP_THREAD_H__
