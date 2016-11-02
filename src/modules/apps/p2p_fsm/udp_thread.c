#include "udp_thread.h"

#include <sys/types.h>
#include <sys/socket.h>

#include "simple_json.h"

#define SOCKADDRLEN     sizeof(struct sockaddr)


int chan_udp[2]; // udp线程通道


void on_channel_udp(lts_socket_t *cs)
{
    char buf[512];

    cs->readable = 0;
    recv(cs->fd, buf, 512, 0);
    fprintf(stderr, "udp data\n");
}


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
        uint8_t clt[SOCKADDRLEN] = {0};
        socklen_t clt_len = SOCKADDRLEN;

        rcv_sz = recvfrom(lsn, buf, sizeof(buf), 0,
                          (struct sockaddr *)clt, &clt_len);
        if (rcv_sz < 1) {
            continue;
        }

        send(chan_udp[1], buf, rcv_sz, 0);
    }

    return NULL;
}
