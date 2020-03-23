/* uSockets is entierly opaque so we can use the real header straight up */
#include "../uWebSockets.js/uWebSockets/uSockets/src/libusockets.h"

int connections = 0;
int disconnections = 0;

#include <stdio.h>

struct us_socket_t *on_open(struct us_socket_t *s, int is_client, char *ip, int ip_length) {
    connections++;

    if (connections % 100 == 0) {
        printf("Connections: %d, disconnections: %d\n", connections, disconnections);
    }

    /* We need a ping every now and then */
    us_socket_timeout(0, s, 32);

    return s;
}

struct us_socket_t *on_close(struct us_socket_t *s) {
    disconnections++;

    if (connections % 100 == 0) {
        printf("Connections: %d, disconnections: %d\n", connections, disconnections);
    }

    return s;
}

struct us_socket_t *on_data(struct us_socket_t *s, char *data, int length) {
    /* Gettings data (ping) resets the timeout */
    us_socket_timeout(0, s, 32);

    /* For now we try and send data also */
    //us_socket_write()

    return s;
}

struct us_socket_t *on_timeout(struct us_socket_t *s) {

    us_socket_close(0, s);

    return s;
}

int main() {
    struct us_loop_t *loop = us_create_loop(NULL, NULL, NULL, NULL, 0);

    struct us_socket_context_options_t options = {};
    struct us_socket_context_t *context = us_create_socket_context(0, loop, 0, options);

    us_socket_context_on_open(0, context, on_open);
    us_socket_context_on_close(0, context, on_close);
    us_socket_context_on_data(0, context, on_data);
    us_socket_context_on_timeout(0, context, on_timeout);

    us_socket_context_listen(0, context, NULL, 4000, 0, 0);

    us_loop_run(loop);
}
