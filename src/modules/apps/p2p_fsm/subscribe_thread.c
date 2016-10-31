#include "common.h"

#include <hiredis.h>


static redisContext *s_rds;

extern redisContext *redisGetConnection(void);
extern redisContext *redisCheckConnection(redisContext *rds);


void *subscribe_thread(void *arg)
{
    while (TRUE) {
        if (NULL == s_rds) {
            s_rds = redisGetConnection();
        }
        if (NULL == s_rds) {
            sleep(10);
            continue;
        }

        if (NULL == redisCheckConnection(s_rds)) {
            redisFree(s_rds);
            s_rds = NULL;
            sleep(10);
            continue;
        }

        sleep(1);
    }

    return NULL;
}
