#ifndef INTERNAL_H
#define INTERNAL_H

#include "Packets.h"

struct us_socket_context_t {
    alignas(16) struct us_loop_t *loop;

    struct us_socket_t *(*on_open)(struct us_socket_t *s, int is_client, char *ip, int ip_length);
    struct us_socket_t *(*on_close)(struct us_socket_t *s);
    struct us_socket_t *(*on_data)(struct us_socket_t *s, char *data, int length);
    struct us_socket_t *(*on_writable)(struct us_socket_t *s);
    struct us_socket_t *(*on_timeout)(struct us_socket_t *s);
    struct us_socket_t *(*on_end)(struct us_socket_t *s);
};

struct us_socket_t {


    //struct us_socket_t *next;

    void *userData;

    // per socket data
    uint32_t networkIp; // this is THEIR IP!
    uint16_t hostPort; // this is THEIR port

    // this is OUR IP!
    uint32_t networkDestinationIp;
    uint16_t hostDestinationPort;

    uint32_t hostAck;
    uint32_t hostSeq;


    /*enum {
        CLOSED,
        SYN_ACK_SENT,
        SYN_SENT,
        ESTABLISHED
    } state;*/

    int state;


    uint32_t originalHostAck;


    alignas(16) struct us_socket_context_t *context;

    int closed;
    int shutdown;
    int wants_writable;
};

void us_internal_socket_context_send_packet(struct us_socket_context_t *context, uint32_t hostSeq, uint32_t hostAck, uint32_t networkDestIp, uint32_t networkSourceIp, int hostDestPort,
                 int hostSourcePort, int flagAck, int flagSyn, int flagFin, int flagRst, char *data, size_t length);

#define _GNU_SOURCE         /* See feature_test_macros(7) */
      #include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <time.h>

struct mmsghdr {
         struct msghdr msg_hdr;  /* Message header */
         unsigned int  msg_len;  /* Number of received bytes for header */
     };

struct us_loop_t {

    /* We only support one listen socket */
    alignas(16) struct us_listen_socket_t *listen_socket;

    /* The list of closed sockets */
    struct us_socket_t *close_list;

    // här sparar vi senaste skapade contextet
    struct us_socket_context_t *context;

    /* Post and pre callbacks */
    void (*pre_cb)(struct us_loop_t *loop);
    void (*post_cb)(struct us_loop_t *loop);

    void (*wakeup_cb)(struct us_loop_t *loop);

    // vi har

    int fd;
    char *buffer[500];
    size_t length[500];
    struct mmsghdr msgs[500];
    struct iovec iovecs[500];

    struct iovec messages[500];

    char *outBuffer[500];
    int queuedBuffersNum;// = 0;
};

// passing data from ip to tcp layer

void us_internal_socket_context_read_tcp(struct us_socket_context_t *context, IpHeader *ipHeader, struct TcpHeader *tcpHeader, int length);

#endif // INTERNAL_H
