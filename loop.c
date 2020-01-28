// loop implements the getting of IP packets and distributing of events

/* uSockets is entierly opaque so we can use the real header straight up */
#include "../uWebSockets.js/uWebSockets/uSockets/src/libusockets.h"


#include "internal.h"

// should be more like read() from a file
int fetchPackageBatch(struct us_loop_t *loop) {
    return recvmmsg(loop->fd, loop->msgs, 500, MSG_WAITFORONE, NULL);
}

IpHeader *getIpPacket(struct us_loop_t *loop, int index, unsigned int *length) {
    IpHeader *ipHeader = (IpHeader *) loop->iovecs[index].iov_base;


    *length = loop->iovecs[index].iov_len;


    return ipHeader;

}

IpHeader *getIpPacketBuffer(struct us_loop_t *loop) {
    if (loop->queuedBuffersNum == 500) {
        //std::cout << "Releasing IP buffers in getIpPacketBuffer" << std::endl;
        //releasePackageBatch();
    }

    return (IpHeader *) loop->outBuffer[loop->queuedBuffersNum++];
}

struct us_loop_t *us_create_loop(void *hint, void (*wakeup_cb)(struct us_loop_t *loop), void (*pre_cb)(struct us_loop_t *loop), void (*post_cb)(struct us_loop_t *loop), unsigned int ext_size) {
    struct us_loop_t *loop = (struct us_loop_t *) malloc(sizeof(struct us_loop_t) + ext_size);

    loop->listen_socket = 0;
    loop->close_list = 0;

    loop->pre_cb = pre_cb;
    loop->post_cb = post_cb;
    loop->wakeup_cb = wakeup_cb;


        printf("creating loop: %p\n", loop);


    //

    loop->fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP); // close(fd) vid stängning
    if (loop->fd == -1) {
        //throw IP_ERR;

        printf("Kan inte skapa IP socket!\n");
        exit(0);
    }

    for (int i = 0; i < 500; i++) {
        loop->buffer[i] = malloc(1024 * 32);
        loop->outBuffer[i] = malloc(1024 * 32);
    }

    printf("Loop's first out buffer is: %p\n", loop->outBuffer[0]);

    const int VLEN = 500;

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

void us_loop_run(struct us_loop_t *loop) {
    printf("Getting ip packets now\n");

    while (1) {
        int messages = fetchPackageBatch(loop);

        int port = 4000;

        for (int i = 0; i < messages; i++) {
            unsigned int length;
            IpHeader *ipHeader = getIpPacket(loop, i, &length);
            struct TcpHeader *tcpHeader = (struct TcpHeader *) IpHeader_getData(ipHeader);//ipHeader->getData();

            // för alla socket contexts, låt dem kolla om de lyssnar på denna porten?
            if (TcpHeader_getDestinationPort(tcpHeader) == port || /*tcpHeader->getDestinationPort()*/TcpHeader_getDestinationPort(tcpHeader) == 4001) {
                us_internal_socket_context_read_tcp(loop->context, ipHeader, tcpHeader, length);
            }
        }

        //printf("about to release now!\n");

        // release aka send
        if (loop->queuedBuffersNum) {
            struct mmsghdr sendVec[500] = {};
            struct sockaddr_in sin[500] = {};

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

            sendmmsg(loop->fd, sendVec, loop->queuedBuffersNum, 0);
            loop->queuedBuffersNum = 0;

            //std::cout << "Sent now" << std::endl;
        }
    }
}
