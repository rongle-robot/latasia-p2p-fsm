#include "common.h"
#include <sys/types.h>
#include <sys/socket.h>

#include <hiredis.h>


static redisContext *s_rds;

int chan_sub[2]; // 订阅线程通道
extern redisContext *redisGetConnection(void);
extern redisContext *redisCheckConnection(redisContext *rds);


void *subscribe_thread(void *arg)
{
    while (TRUE) {
        redisReply *reply;

        if (NULL == s_rds) {
            s_rds = redisGetConnection();
        }
        if (NULL == s_rds) {
            sleep(10);
            continue;
        }

        if (NULL == redisCheckConnection(s_rds)) {
            fprintf(stderr, "invalid connection\n");
            redisFree(s_rds);
            s_rds = NULL;
            sleep(10);
            continue;
        }

        reply = redisCommand(s_rds, "SUBSCRIBE test");
        freeReplyObject(reply);

        while(redisGetReply(s_rds, (void **)&reply) == REDIS_OK) {
            switch (reply->type) {
            case REDIS_REPLY_STATUS:
                break;
            case REDIS_REPLY_STRING:
                break;
            case REDIS_REPLY_ERROR:
                break;
            case REDIS_REPLY_NIL:
                break;
            case REDIS_REPLY_INTEGER:
                break;
            case REDIS_REPLY_ARRAY:
                // 0: message
                // 1: 订阅的主题名称
                // 2: 推送的数据
                fprintf(stderr, "%s\n", reply->element[2]->str);
                uintptr_t x=123;
                send(chan_sub[1], &x, sizeof(x), 0);
                break;

            default:
                break;
            }
            freeReplyObject(reply);
        }
    }

    return NULL;
}
