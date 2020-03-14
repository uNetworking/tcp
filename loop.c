// loop implements the getting of IP packets and distributing of events



/* uSockets is entierly opaque so we can use the real header straight up */
#include "../uWebSockets.js/uWebSockets/uSockets/src/libusockets.h"


#include "internal.h"

// we print statistics such as numer of out of sync, number of "healed" sockets due to drop, etc
void print_statistics(struct us_loop_t *loop) {
    printf("Packets out of order so far: %d\n", loop->packets_out_of_order);
    printf("Healed sockets so far: %d\n", loop->healed_sockets);
    printf("Duplicated packets: %d\n\n", loop->duplicated_packets);
    printf("Packets received: %lld\n", loop->packets_received);
}

#include <sys/time.h>

// should be more like read() from a file
int fetchPackageBatch(struct us_loop_t *loop) {
    // wait for one is pointless here
    return recvmmsg(loop->fd, loop->msgs, 1024, /*MSG_WAITFORONE*/ 0, 0);
}

void releaseSend(struct us_loop_t *loop) {


    // this is not blocking, should block!

        // release aka send
    if (loop->queuedBuffersNum) {

        //printf("Sending %d packages\n", loop->queuedBuffersNum);

        struct mmsghdr sendVec[1024] = {};
        struct sockaddr_in sin[1024] = {};

        int packages = loop->queuedBuffersNum;

        for (int i = 0; i < loop->queuedBuffersNum; i++) {

            IpHeader *ipHeader = (IpHeader *) loop->outBuffer[i];
            struct TcpHeader *tcpHeader = (struct TcpHeader *) IpHeader_getData(ipHeader);

            int length = IpHeader_getTotalLength(ipHeader);//ipHeader->getTotalLength();

            sin[i].sin_family = AF_INET;
            sin[i].sin_port = tcpHeader->header.dest;
            sin[i].sin_addr.s_addr = ipHeader->daddr;

            loop->messages[i].iov_base = ipHeader;
            loop->messages[i].iov_len = length;

            // send out of order!
//            sendVec[i].msg_hdr.msg_iov = &messages[packages - i - 1];
//            sendVec[i].msg_hdr.msg_iovlen = 1;

//            sendVec[i].msg_hdr.msg_name = &sin[packages - i - 1];
//            sendVec[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);

            sendVec[i].msg_hdr.msg_iov = &loop->messages[i];
            sendVec[i].msg_hdr.msg_iovlen = 1;

            sendVec[i].msg_hdr.msg_name = &sin[i];
            sendVec[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);

        }

        int sent = 0;

        // we just block until we're done
        while (sent != loop->queuedBuffersNum) {
            int tmp = sendmmsg(loop->send_fd, &sendVec[sent], loop->queuedBuffersNum - sent, 0);
            if (tmp > 0) {
                sent += tmp;
            }

            /*if (tmp != loop->queuedBuffersNum) {
                printf("COULD NOT SEND ALL PACKETS WITHOUT BLOCKING!\n");
                exit(0);
            }*/

            //break;
        }
        


        loop->queuedBuffersNum = 0;

        //std::cout << "Sent now" << std::endl;
    }
}

IpHeader *getIpPacket(struct us_loop_t *loop, int index, unsigned int *length) {
    IpHeader *ipHeader = (IpHeader *) loop->iovecs[index].iov_base;


    *length = loop->iovecs[index].iov_len;


    return ipHeader;

}

IpHeader *getIpPacketBuffer(struct us_loop_t *loop) {
    if (loop->queuedBuffersNum == 1024) {
        //std::cout << "Releasing IP buffers in getIpPacketBuffer" << std::endl;
        printf("SENDING OVERFLOW!\n");
        exit(0);
        releaseSend(loop);
    }

    return (IpHeader *) loop->outBuffer[loop->queuedBuffersNum++];
}

#include <sys/epoll.h>

void us_internal_loop_link(struct us_loop_t *loop, struct us_socket_context_t *context) {
    /* Insert this context as the head of loop */
    context->next = loop->head;
    context->prev = 0;
    if (loop->head) {
        loop->head->prev = context;
    }
    loop->head = context;
}

struct us_loop_t *us_create_loop(void *hint, void (*wakeup_cb)(struct us_loop_t *loop), void (*pre_cb)(struct us_loop_t *loop), void (*post_cb)(struct us_loop_t *loop), unsigned int ext_size) {
    struct us_loop_t *loop = (struct us_loop_t *) malloc(sizeof(struct us_loop_t) + ext_size);

    loop->listen_socket = 0;
    loop->context = 0;
    loop->close_list = 0;

    loop->pre_cb = pre_cb;
    loop->post_cb = post_cb;
    loop->wakeup_cb = wakeup_cb;

    loop->head = 0;
    loop->iterator = 0;

    //

    loop->packets_out_of_order = 0;
    loop->healed_sockets = 0;
    loop->duplicated_packets = 0;
    loop->packets_received = 0;

    loop->epfd = epoll_create1(0);

    loop->timer = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.u64 = 0; // mark the timer as 0
    epoll_ctl(loop->epfd, EPOLL_CTL_ADD, loop->timer, &event);

    /* An IPPROTO_RAW socket is send only.  If you really want to receive
       all IP packets, use a packet(7) socket with the ETH_P_IP protocol.
       Note that packet sockets don't reassemble IP fragments, unlike raw
       sockets. */

    loop->send_fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

    loop->fd = socket(AF_INET, SOCK_RAW | SOCK_NONBLOCK, IPPROTO_TCP); // close(fd) vid stängning
    if (loop->fd == -1) {
        //throw IP_ERR;

        printf("Kan inte skapa IP socket!\n");
        exit(0);
    }

    //struct epoll_event event;
    event.events = EPOLLIN;
    event.data.u64 = 1; // mark the raw socket as 1
    epoll_ctl(loop->epfd, EPOLL_CTL_ADD, loop->fd, &event);

    for (int i = 0; i < 1024; i++) {
        loop->buffer[i] = malloc(1024 * 32);
        loop->outBuffer[i] = malloc(1024 * 32);
    }

    printf("Loop's first out buffer is: %p\n", loop->outBuffer[0]);

    const int VLEN = 1024;

    memset(loop->msgs, 0, sizeof(loop->msgs));
    for (int i = 0; i < VLEN; i++) {
        loop->iovecs[i].iov_base         = loop->buffer[i];
        loop->iovecs[i].iov_len          = 1024 * 32;
        loop->msgs[i].msg_hdr.msg_iov    = &loop->iovecs[i];
        loop->msgs[i].msg_hdr.msg_iovlen = 1;
    }

    int one = 1;
    const int *val = &one;
    if (setsockopt (loop->fd, IPPROTO_IP, IP_HDRINCL, val, sizeof (one)) < 0) {
        //throw IP_ERR;
    }

    loop->queuedBuffersNum = 0;

    return loop;
}

void us_wakeup_loop(struct us_loop_t *loop) {
    /* We do this immediately as of now, could be delayed to next iteration */
    loop->wakeup_cb(loop);
}

void us_loop_free(struct us_loop_t *loop) {
    free(loop);
}

void *us_loop_ext(struct us_loop_t *loop) {
    return loop + 1;
}

#include "internal.h"

// we have this somewhere
extern struct us_socket_t *global_s;

/* Either the loop holds all sockets or the socket context do? */
#include "uthash.h"

/*
 *
 * #define HASH_FIND_INT(head,findint,out)                                          \
    HASH_FIND(hh,head,findint,sizeof(int),out)*/

// us_internal_socket_context_read_tcp borde returnera själva socketen antingen utbytt, skapad eller så
// sen kan vi i loopen hålla koll på alla sockets i en hashmap från 64+32 = 96 bit hash key = 12 bytes



// this is the hash table
struct us_socket_t *sockets = NULL;

void remove_socket(struct us_socket_t *s) {

    HASH_DEL(sockets, s);

    free(s);

    global_s = 0;
}

/* We also need to merge this with uSockets and enable TLS over it */
/* WITH_USERSPACE=1 */

/* We need to have timeout by now, and every tick should print statistics on packet loss */
void us_loop_run(struct us_loop_t *loop) {
    printf("Getting ip packets now\n");

    int repeat_ms = 4000;
    int ms = 4000;

    struct itimerspec timer_spec = {
        {repeat_ms / 1000, ((long)repeat_ms * 1000000) % 1000000000},
        {ms / 1000, ((long)ms * 1000000) % 1000000000}
    };

    timerfd_settime(loop->timer, 0, &timer_spec, NULL);

    while(1) {
        // epoll_wait on two fds: time and raw socket
        struct epoll_event events[2];
        int numEvents = epoll_wait(loop->epfd, events, 2, -1);

        for (int i = 0; i < numEvents; i++) {
            if (events[i].data.u64 == 0) {
                //printf("Timerfd tick!\n");

                print_statistics(loop);

                us_internal_timer_sweep(loop);

                uint64_t buf;
                read(loop->timer, &buf, 8);
            }

            if (events[i].data.u64 == 1) {
                // a packet



                int messages = fetchPackageBatch(loop);

                // should never happen
                if (messages == -1) {
                    continue;
                }

                //printf("Read %d packets\n", messages);

                // this should never happen, if it does we read too slowly and lag behind (but should be reset whatever we miss anyways)
                if (messages == 1024) {
                    printf("WE ARE NOT READING PACKAGES FAST ENOUGH!\n");
                    //exit(0);
                }

                for (int i = 0; i < messages; i++) {
                    unsigned int length;
                    IpHeader *ipHeader = getIpPacket(loop, i, &length);

                    /* First we filter out everything that isn't tcp over ipv4 */
                    if (ipHeader->version != 4 || ipHeader->protocol != IPPROTO_TCP) {
                        continue;
                    }

                    /* Now we know this is tcp */
                    struct TcpHeader *tcpHeader = (struct TcpHeader *) IpHeader_getData(ipHeader);

                    /* Is this packet SYN? */
                    if (tcpHeader->header.syn && !tcpHeader->header.ack) {
                        /* Loop over all contexts */
                        for (struct us_socket_context_t *context = loop->context; context; context = context->next) {
                            /* Loop over all listen sockets */
                            for (struct us_listen_socket_t *listen_socket = context->listen_socket; listen_socket; ) {

                                if (listen_socket->port == TcpHeader_getDestinationPort(tcpHeader)) {
                                    //global_s = 0;

                                    us_internal_socket_context_read_tcp(NULL, listen_socket->context, ipHeader, tcpHeader, length);

                                    // vi kommer att ha ändrat global_s om det finns en ny socket!
                                    if (global_s) {
                                        struct SOCKET_KEY key = {
                                            TcpHeader_getSourcePort(tcpHeader),
                                            TcpHeader_getDestinationPort(tcpHeader),
                                            ipHeader->saddr,
                                            ipHeader->daddr
                                        };

                                        global_s->key = key;

                                        HASH_ADD(hh, sockets, key, sizeof(struct SOCKET_KEY), global_s);

                                        global_s = 0;
                                    }

                                }

                                /* We only have one listen socket for now */
                                break;
                            }
                        }
                    } else {

                        struct us_socket_t *s;
                        struct SOCKET_KEY key = {
                            TcpHeader_getSourcePort(tcpHeader),
                            TcpHeader_getDestinationPort(tcpHeader),
                            ipHeader->saddr,
                            ipHeader->daddr
                        };

                        HASH_FIND(hh, sockets, &key, sizeof(struct SOCKET_KEY), s);

                        if (s) {
                            us_internal_socket_context_read_tcp(s, s->context, ipHeader, tcpHeader, length);
                        }

                    }
                }

                releaseSend(loop);
            }
        }

    }
}


// vi behöver få in timers, och svepa över de websockets som inte får något meddelande i tid - som en absolut måttstock på stabilitet oavsett outof order etc

void us_internal_timer_sweep(struct us_loop_t *loop) {
    for (loop->iterator = loop->head; loop->iterator; loop->iterator = loop->iterator->next) {

        struct us_socket_context_t *context = loop->iterator;
        for (context->iterator = context->head; context->iterator; ) {

            struct us_socket_t *s = context->iterator;
            if (s->timeout && --(s->timeout) == 0) {

                context->on_socket_timeout(s);

                /* Check for unlink / link */
                if (s == context->iterator) {
                    context->iterator = s->next;
                }
            } else {
                context->iterator = s->next;
            }
        }
    }
}