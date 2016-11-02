#ifndef __APP_UDP_THREAD_H__
#define __APP_UDP_THREAD_H__


#include "mem_pool.h"
#include "adv_string.h"
#include "socket.h"


extern int chan_udp[2]; // udp线程通道
extern void *udp_thread(void *arg); // udp线程回调


extern void on_channel_udp(lts_socket_t *cs);
#endif // __APP_UDP_THREAD_H__
