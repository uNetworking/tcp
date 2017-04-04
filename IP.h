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

struct IP {

    int fd;
    char *buffer[500];
    size_t length[500];
    mmsghdr msgs[500];
    iovec iovecs[500];

    iovec messages[500];

    char *outBuffer[500];
    int queuedBuffersNum = 0;

    IP() {
        fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
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

        int one = 1;
        const int *val = &one;
        if (setsockopt (fd, IPPROTO_IP, IP_HDRINCL, val, sizeof (one)) < 0) {
            throw IP_ERR;
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
