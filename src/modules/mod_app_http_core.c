#include <zlib.h>
#include <arpa/inet.h>
#include <dirent.h>

#include <strings.h>

#include "latasia.h"
#include "rbtree.h"
#include "logger.h"
#include "conf.h"

#define __THIS_FILE__       "src/modules/mod_app_http_core.c"


#define DEFAULT_FILENAME        "index.html"
#define HTTP_404_HEADER         "HTTP/1.1 404 NOT FOUND\r\n"\
                                "Server: latasia\r\n"\
                                "Content-Length: 133\r\n"\
                                "Content-Type: text/html\r\n"\
                                "Connection: close\r\n\r\n"
#define HTTP_404_BODY           "<!DOCTYPE html>"\
                                "<html>"\
                                "<head>"\
                                "<meta charset=\"utf-8\" />"\
                                "<title>404</title>"\
                                "</head>"\
                                "<body>"\
                                "{\"result\": 404, \"message\": not found}"\
                                "</body>"\
                                "</html>"
/*
#define HTTP_200_HEADER         "HTTP/1.1 200 OK\r\n"\
                                "Server: latasia\r\n"\
                                "Content-Length: %lu\r\n"\
                                "Content-Type: application/octet-stream\r\n"\
                                "Connection: keep-alive\r\n\r\n"
*/
#define HTTP_200_HEADER         "HTTP/1.1 200 OK\r\n"\
                                "Server: latasia\r\n"\
                                "Content-Length: %lu\r\n"\
                                "Content-Type: %s\r\n"\
                                "Connection: keep-alive\r\n\r\n"


typedef struct {
    lts_str_t cwd; // 工作目录
} http_conf_t;

typedef struct {
    http_conf_t mod_conf; // 应用模块配置
} http_core_ctx_t;

typedef struct {
    lts_str_t req_path;
    lts_file_t req_file;
} http_req_t;


static http_core_ctx_t s_ctx = {
    {
        // 默认配置
        lts_string("/"),
    },
};


void dump_mem_to_file(char const *filepath, uint8_t *data, size_t n)
{
    FILE *f = fopen(filepath, "wb");
    fwrite(data, n, 1, f);
    fclose(f);
}


static int init_http_core_module(lts_module_t *module)
{
    fprintf(stderr, "conf file:%s\n", lts_main_conf.app_mod_conf.data);
    return 0;
}


static void exit_http_core_module(lts_module_t *module)
{
    return;
}


static void http_core_on_connected(lts_socket_t *s)
{
    return;
}


static void http_core_service(lts_socket_t *s)
{
    lts_str_t idata, req_line;
    int pattern_s;
    lts_str_t pattern;
    lts_str_t uri, *http_cwd;
    lts_pool_t *pool = s->conn->pool;
    lts_buffer_t *rb = s->conn->rbuf, *sb = s->conn->sbuf;
    http_req_t *req;
    size_t n, n_read;

    if (lts_buffer_empty(rb)) {
        abort();
    }

    // 获取请求第一行
    idata.data = rb->start;
    idata.len = (size_t)(rb->last - rb->start);
    pattern = (lts_str_t)lts_string("\r\n");
    pattern_s = lts_str_find(&idata, &pattern, 0);
    if (-1 == pattern_s) {
        (void)lts_write_logger(&lts_file_logger, LTS_LOG_ERROR,
                               "%s:bad request, no enter\n", STR_LOCATION);
        return;
    }
    req_line.data = idata.data;
    req_line.len = pattern_s;

    // 获取请求路径
    uri.data = NULL;
    uri.len = 0;
    for (size_t i = 0; i < req_line.len + 1; ++i) {
        if (uri.data) {
            ++uri.len;
            if (' ' == req_line.data[i]) {
                break;
            }
        } else {
            if ('/' == req_line.data[i]) {
                uri.data = &req_line.data[i];
            }
        }
    }
    if (0 == uri.len) {
        (void)lts_write_logger(&lts_file_logger, LTS_LOG_ERROR,
                               "%s:bad request, no slash\n", STR_LOCATION);
        return;
    }

    // 生成绝对路径
    http_cwd = &s_ctx.mod_conf.cwd;
    req = (http_req_t *)lts_palloc(pool, sizeof(http_req_t));
    req->req_path.len = http_cwd->len + uri.len;
    if ('/' == uri.data[uri.len - 1]) {
        req->req_path.len += sizeof(DEFAULT_FILENAME) - 1;
    }
    req->req_path.data = lts_palloc(pool, req->req_path.len + 1);
    (void)memcpy(req->req_path.data, http_cwd->data, http_cwd->len);
    (void)memcpy(req->req_path.data + http_cwd->len, uri.data, uri.len);
    if ('/' == uri.data[uri.len - 1]) {
        (void)memcpy(req->req_path.data + http_cwd->len + uri.len,
                     DEFAULT_FILENAME, sizeof(DEFAULT_FILENAME) - 1);
    }
    req->req_path.data[req->req_path.len] = 0;
    req->req_file.fd = -1;

    // 清空接收缓冲
    lts_buffer_clear(rb);

    n = sb->end - sb->last;
    req->req_file.name = req->req_path;
    if (-1 == req->req_file.fd) {
        struct stat st;
        lts_str_t ftype;

        // 打开文件
        if (-1 == lts_file_open(&req->req_file, O_RDONLY, S_IWUSR | S_IRUSR,
                                &lts_file_logger)) {
            // 404
            n = sizeof(HTTP_404_HEADER) - 1;
            (void)memcpy(sb->last, HTTP_404_HEADER, n);
            sb->last += n;
            n = sizeof(HTTP_404_BODY) - 1;
            (void)memcpy(sb->last, HTTP_404_BODY, n);
            sb->last += n;

            return;
        }

        s->app_ctx = req;

        // 获取文件大小
        (void)fstat(req->req_file.fd, &st);

        // 获取文件类型
        for (int i = req->req_path.len - 1; i >= 0; --i) {
            if ('.' == req->req_path.data[i]) {
                ftype.data = &req->req_path.data[i + 1];
                ftype.len = req->req_path.len - 1 - i;
                break;
            }
        }

        // 发送http头
        if (sizeof(HTTP_200_HEADER) - 1 > (n + 32)) { // 预留长度字符串
            abort();
        }

        if (0 == strncasecmp((const char *)ftype.data, "html",
                             MIN(ftype.len, sizeof("html") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "text/html");
        } else if (0 == strncasecmp((const char *)ftype.data, "txt",
                                    MIN(ftype.len, sizeof("txt") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "text/plain");
        } else if (0 == strncasecmp((const char *)ftype.data, "log",
                                    MIN(ftype.len, sizeof("log") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "text/plain");
        } else if (0 == strncasecmp((const char *)ftype.data, "xml",
                                    MIN(ftype.len, sizeof("xml") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "text/xml");
        } else if (0 == strncasecmp((const char *)ftype.data, "gif",
                                    MIN(ftype.len, sizeof("gif") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "image/gif");
        } else if (0 == strncasecmp((const char *)ftype.data, "jpg",
                                    MIN(ftype.len, sizeof("jpg") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "image/jpeg");
        } else if (0 == strncasecmp((const char *)ftype.data, "png",
                                    MIN(ftype.len, sizeof("png") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "image/png");
        } else if (0 == strncasecmp((const char *)ftype.data, "ico",
                                    MIN(ftype.len, sizeof("ico") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "image/x-icon");
        } else if (0 == strncasecmp((const char *)ftype.data, "mid",
                                    MIN(ftype.len, sizeof("mid") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "audio/midi");
        } else if (0 == strncasecmp((const char *)ftype.data, "mp3",
                                    MIN(ftype.len, sizeof("mp3") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "audio/mpeg");
        } else if (0 == strncasecmp((const char *)ftype.data, "mp4",
                                    MIN(ftype.len, sizeof("mp4") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "video/mp4");
        } else if (0 == strncasecmp((const char *)ftype.data, "webm",
                                    MIN(ftype.len, sizeof("webm") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "video/webm");
        } else if (0 == strncasecmp((const char *)ftype.data, "flv",
                                    MIN(ftype.len, sizeof("flv") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "video/x-flv");
        } else if (0 == strncasecmp((const char *)ftype.data, "avi",
                                    MIN(ftype.len, sizeof("avi") - 1))) {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size, "video/x-msvideo");
        } else {
            n_read = snprintf((char *)sb->last, n,
                              HTTP_200_HEADER, st.st_size,
                              "application/octet-stream");
        }
        if (n_read > 0) {
            sb->last += n_read;
        }
    }

    return;
}


static void http_core_send_more(lts_socket_t *s)
{
    size_t n, n_read;
    http_req_t *req;
    lts_buffer_t *sb;

    sb = s->conn->sbuf;

    req = s->app_ctx;
    if (NULL == req) {
        return;
    }

    // 读文件数据
    if (lts_buffer_full(sb)) {
        return;
    }
    n = sb->end - sb->last;
    n_read = lts_file_read(&req->req_file, sb->last, n, &lts_file_logger);
    if (n_read > 0) {
        sb->last += n_read;
        return;
    }

    // 完毕
    lts_file_close(&req->req_file);

    return;
}


static lts_app_module_itfc_t http_core_itfc = {
    &http_core_on_connected,
    &http_core_service,
    &http_core_send_more,
    NULL,
};

lts_module_t lts_app_http_core_module = {
    lts_string("lts_app_http_core_module"),
    LTS_APP_MODULE,
    &http_core_itfc,
    NULL,
    &s_ctx,

    // module interfaces {{
    NULL,
    &init_http_core_module,
    &exit_http_core_module,
    NULL,
    // }} module interfaces
};
