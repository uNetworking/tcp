/* uSockets is entierly opaque so we can use the real header straight up */
#include "../uWebSockets.js/uWebSockets/uSockets/src/libusockets.h"

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>

int us_socket_write(int ssl, struct us_socket_t *s, const char *data, int length, int msg_more) {

    if (!length) {
        return 0;
    }

    us_internal_socket_context_send_packet(s->context, s->hostSeq, s->hostAck, s->networkIp, s->networkDestinationIp, s->hostPort, s->hostDestinationPort, 1, 0, 0, 0, data, length);
    s->hostSeq += length;

    /* Send everything */
    return length;
}

void us_socket_timeout(int ssl, struct us_socket_t *s, unsigned int seconds) {
    //printf("socket timeout: %d\n", seconds);
}

void *us_socket_ext(int ssl, struct us_socket_t *s) {
    //printf("socket ext: %p\n", s + 1);
    return s + 1;
}

struct us_socket_context_t *us_socket_context(int ssl, struct us_socket_t *s) {
    //printf("Socket context: %p\n", s->context);
    return s->context;
}

void us_socket_flush(int ssl, struct us_socket_t *s) {

}

void us_socket_shutdown(int ssl, struct us_socket_t *s) {
    s->shutdown = 1;
}

int us_socket_is_shut_down(int ssl, struct us_socket_t *s) {
    return s->shutdown;
}

int us_socket_is_closed(int ssl, struct us_socket_t *s) {
    return s->closed;
}

struct us_socket_t *us_socket_close(int ssl, struct us_socket_t *s) {

    if (!us_socket_is_closed(0, s)) {
        /* Emit close event */
        s = s->context->on_close(s);
    }

    /* We are now closed */
    s->closed = 1;

    /* Add us to the close list */

    return s;
}

void us_socket_remote_address(int ssl, struct us_socket_t *s, char *buf, int *length) {
    *length = 0;
}
