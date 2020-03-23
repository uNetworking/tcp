/* uSockets is entierly opaque so we can use the real header straight up */
#include "../uWebSockets.js/uWebSockets/uSockets/src/libusockets.h"

int connections = 0;

#include <stdio.h>

struct us_socket_t *on_open(struct us_socket_t *s, int is_client, char *ip, int ip_length) {
    printf("TCP socket accepted\n");

    return s;
}

struct us_socket_t *on_close(struct us_socket_t *s) {
    printf("TCP socket closed\n");

    return s;
}

struct us_socket_t *on_data(struct us_socket_t *s, char *data, int length) {
    printf("TCP socket data with length: %d\n", length);

    // echo it back
    us_socket_write(0, s, data, length, 0);

    return s;
}

int main() {
    struct us_loop_t *loop = us_create_loop(NULL, NULL, NULL, NULL, 0);

    struct us_socket_context_options_t options = {};
    struct us_socket_context_t *context = us_create_socket_context(0, loop, 0, options);

    us_socket_context_on_open(0, context, on_open);
    us_socket_context_on_close(0, context, on_close);
    us_socket_context_on_data(0, context, on_data);

    us_socket_context_listen(0, context, "127.0.0.1", 4000, 0, 0);
    us_loop_run(loop);
}
