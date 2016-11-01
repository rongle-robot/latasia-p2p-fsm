#include "logic_callback.h"

#include "rbmap.h"
#include "protocol_sjsonb.h"
#include "rbt_timer.h"


void make_simple_rsp(char *error_no, char *error_msg,
                     lts_buffer_t *sbuf, lts_pool_t *pool)
{
    lts_sjson_t rsp = lts_empty_sjson(pool);

    lts_sjson_add_kv(&rsp, "errno", error_no);
    lts_sjson_add_kv(&rsp, "errmsg", error_msg);
    lts_proto_sjsonb_encode(&rsp, sbuf, pool);

    return;
}
