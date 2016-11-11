#ifndef __APP_P2P_FSM_H__
#define __APP_P2P_FSM_H__


#include "simple_json.h"
#include "socket.h"

#define E_SUCCESS           "200"
#define E_NOT_EXIST         "404"
#define E_NOT_SUPPORT       "405"
#define E_EXIST             "424"
#define E_ARG_MISS          "421"
#define E_INVALID_ARG       "422"
#define E_SERVER_FAILED     "500"

#define K_ERRNO             "error_no"
#define K_ERRMSG            "error_msg"


extern void make_simple_rsp(char *error_no, char *error_msg,
                            lts_buffer_t *sbuf, lts_pool_t *pool);
#endif // __APP_P2P_FSM_H__
