#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <bare.h>
#include <js.h>
#include <uv.h>

typedef struct {
    js_env_t* env;
    js_deferred_t* deferred;
    uv_tcp_t socket;
    uv_connect_t connect_req;
    uv_write_t write_req;
    char* message;
    size_t message_len;
    char* response_data;
    size_t response_len;
    size_t response_capacity;
} tcp_request_t;

void on_close(uv_handle_t* handle) {
    tcp_request_t* req = (tcp_request_t*)handle->data;
    
    if (req->message) {
        free(req->message);
    }
    if (req->response_data) {
        free(req->response_data);
    }
    free(req);
}

void reject_request(tcp_request_t* req, const char* error_msg) {
    js_handle_scope_t* scope;
    int err = js_open_handle_scope(req->env, &scope);
    assert(err == 0);

    js_value_t* code_val = NULL;
    js_value_t* msg_val;
    err = js_create_string_utf8(req->env, (const utf8_t*)error_msg, -1, &msg_val);
    
    if (err == 0) {
        js_value_t* error;
        err = js_create_error(req->env, code_val, msg_val, &error);
        if (err == 0) {
            js_reject_deferred(req->env, req->deferred, error);
        }
    }
    
    js_close_handle_scope(req->env, scope);
    uv_close((uv_handle_t*)&req->socket, on_close);
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

void on_write(uv_write_t* write_req, int status) {
    tcp_request_t* req = (tcp_request_t*)write_req->data;
    
    if (status < 0) {
        fprintf(stderr, "[ERROR] Write failed: %s\n", uv_strerror(status));
        reject_request(req, "Write to TCP socket failed");
    }
}

void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    tcp_request_t* req = (tcp_request_t*)stream->data;

    if (nread > 0) {
        if (req->response_len + nread > req->response_capacity) {
            req->response_capacity = (req->response_len + nread) * 2;
            req->response_data = realloc(req->response_data, req->response_capacity);
            assert(req->response_data);
        }
        
        memcpy(req->response_data + req->response_len, buf->base, nread);
        req->response_len += nread;
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            js_handle_scope_t* scope;
            int err = js_open_handle_scope(req->env, &scope);
            assert(err == 0);

            js_value_t* arraybuffer;
            void* ab_data;
            err = js_create_arraybuffer(req->env, req->response_len, &ab_data, &arraybuffer);
            
            if (err == 0) {
                memcpy(ab_data, req->response_data, req->response_len);
                js_resolve_deferred(req->env, req->deferred, arraybuffer);
            } else {
                js_close_handle_scope(req->env, scope);
                reject_request(req, "Failed to allocate JS ArrayBuffer");
                goto cleanup;
            }

            js_close_handle_scope(req->env, scope);
        } else {
            fprintf(stderr, "[ERROR] Read error: %s\n", uv_err_name(nread));
            reject_request(req, "Socket read error");
            goto cleanup;
        }
        
        uv_close((uv_handle_t*)&req->socket, on_close);
    }

cleanup:
    if (buf->base) {
        free(buf->base);
    }
}

void on_connect(uv_connect_t* connect_req, int status) {
    tcp_request_t* req = (tcp_request_t*)connect_req->data;

    if (status < 0) {
        fprintf(stderr, "[ERROR] Connection failed: %s\n", uv_strerror(status));
        reject_request(req, "Connection failed");
        return;
    }

    int err = uv_read_start((uv_stream_t*)&req->socket, alloc_buffer, on_read);
    if (err < 0) {
        reject_request(req, "Failed to start reading from socket");
        return;
    }

    uv_buf_t write_buf = uv_buf_init(req->message, req->message_len);
    err = uv_write(&req->write_req, (uv_stream_t*)&req->socket, &write_buf, 1, on_write);
    if (err < 0) {
        reject_request(req, "Failed to write payload to socket");
    }
}

js_value_t* bare_tcp_cat(js_env_t* env, js_callback_info_t* info) {
    int err;
    size_t argc = 3;
    js_value_t* argv[3];
    char ip[64];
    size_t ip_len;
    int32_t port;
    size_t message_size;
    tcp_request_t* req;
    js_value_t* promise;
    uv_loop_t* loop;
    struct sockaddr_in addr;

    err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
    assert(err == 0);
    assert(argc == 3);

    err = js_get_value_string_utf8(env, argv[0], (utf8_t*)ip, sizeof(ip), &ip_len);
    assert(err == 0);

    err = js_get_value_int32(env, argv[1], &port);
    assert(err == 0);

    err = js_get_value_string_utf8(env, argv[2], NULL, 0, &message_size);
    assert(err == 0);

    req = malloc(sizeof(tcp_request_t));
    assert(req);
    
    req->env = env;
    req->message_len = message_size;
    req->message = malloc(message_size + 1);
    assert(req->message);
    
    err = js_get_value_string_utf8(env, argv[2], (utf8_t*)req->message, message_size + 1, NULL);
    assert(err == 0);

    req->response_len = 0;
    req->response_capacity = 4096;
    req->response_data = malloc(req->response_capacity);
    assert(req->response_data);

    err = js_create_promise(env, &req->deferred, &promise);
    assert(err == 0);

    err = js_get_env_loop(env, &loop);
    assert(err == 0);

    err = uv_tcp_init(loop, &req->socket);
    if (err < 0) {
        reject_request(req, "Failed to initialize TCP socket");
        return promise;
    }

    req->socket.data = req;
    req->connect_req.data = req;
    req->write_req.data = req;

    err = uv_ip4_addr(ip, port, &addr);
    if (err < 0) {
        reject_request(req, "Invalid IP address format");
        return promise;
    }

    err = uv_tcp_connect(&req->connect_req, &req->socket, (const struct sockaddr*)&addr, on_connect);
    if (err < 0) {
        reject_request(req, "Failed to initiate connection process");
    }

    return promise;
}

js_value_t* init(js_env_t* env, js_value_t* exports) {
    int err;
    js_value_t* fn;
    
    err = js_create_function(env, "tcpCat", -1, bare_tcp_cat, NULL, &fn);
    assert(err == 0);

    err = js_set_named_property(env, exports, "tcpCat", fn);
    assert(err == 0);

    return exports;
}

BARE_MODULE(bare_addon, init)