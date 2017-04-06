#ifndef IP_H
#define IP_H

// Low-perf test IP driver
// Port this to DPDK when performance matters

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

#include <iostream>
#include <vector>

enum {
    IP_ERR
};

struct IpHeader : iphdr {

    int getVersion() {
        return version;
    }

    void *getData() {
        return ((char *) this) + getHeaderLength();
    }

    int getHeaderLength() {
        return ihl * 4;
    }

    // fel
    int getTotalLength() {
        return ntohs(tot_len);
    }

    int getFragmentOffset() {
        return ntohs(frag_off);
    }
};

#include <linux/if_ether.h>

struct IP {

    int fd, fd_send;
    char *buffer[500];
    size_t length[500];
    mmsghdr msgs[500];
    iovec iovecs[500];

    iovec messages[500];

    char *outBuffer[500];
    int queuedBuffersNum = 0;

    IP() {
        // receive
        fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));

        // send (move over to packet socket later)
        fd_send = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

        if (fd == -1) {
            throw IP_ERR;
        }

        for (int i = 0; i < 500; i++) {
            buffer[i] = new char[1024 * 4];

            outBuffer[i] = new char[1024 * 4];
        }

        const int VLEN = 500;

        memset(msgs, 0, sizeof(msgs));
        for (int i = 0; i < VLEN; i++) {
            iovecs[i].iov_base         = buffer[i];
            iovecs[i].iov_len          = 1024 * 4;
            msgs[i].msg_hdr.msg_iov    = &iovecs[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
        }
    }

    ~IP() {
        //delete [] buffer;
        close(fd);
    }

    void releasePackageBatch();
    int fetchPackageBatch() {
        return recvmmsg(fd, msgs, 500, MSG_WAITFORONE, nullptr);
    }

    IpHeader *getIpPacket(int index) {
        return (IpHeader *) iovecs[index].iov_base;
    }

    IpHeader *getIpPacketBuffer() {
        if (queuedBuffersNum == 500) {
            releasePackageBatch();
        }
        return (IpHeader *) outBuffer[queuedBuffersNum++];
    }
};

#endif // IP_H
