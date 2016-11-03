#ifndef __APP_SUBSCRIBE_THREAD_H__
#define __APP_SUBSCRIBE_THREAD_H__


#include "mem_pool.h"
#include "adv_string.h"


typedef struct {
    int retinue_master; // bool
    lts_pool_t *pool;
    lts_str_t *auth;
    int port_restricted; // bool
    int udp_port;
} sub_chanpack_t; //channel通信结构


extern int chan_sub[2]; // 订阅线程通道
extern void *subscribe_thread(void *arg); // 订阅线程回调
#endif // __APP_SUBSCRIBE_THREAD_H__
