#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <uv.h>

typedef struct {
    uv_loop_t* loop;
    uv_tcp_t socket;
} Network;

Network network = {0};

void on_connect(uv_connect_t *req, int status) {
    if(status<0){
        printf("[ERROR] could not connect: %s\n", uv_strerror(status));
        free(req);
        return;
    }

    printf("[INFO] client connected...\n");
    //uv_read_start(req->handle,);
    free(req);
}

void network_init() {
    printf("[INFO] creating a new client...\n");

    network.loop = uv_default_loop();

    int err = uv_tcp_init(network.loop, &network.socket);
    if (err) {
        printf("[ERROR] could not init tcp socket: %s\n", uv_strerror(err));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    err = uv_ip4_addr("1.1.1.1", 80, &addr);
    if (err) {
        printf("[ERROR] could not create ip addr: %s\n", uv_strerror(err));
        exit(EXIT_FAILURE);
    }

    uv_connect_t *connect = malloc(sizeof(*connect));
    assert(connect);

    uv_tcp_connect(connect, &network.socket, (const struct sockaddr*)&addr, on_connect);
}

void network_close() {
    uv_loop_close(network.loop);
}

void network_launch() {
    
}