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

#include <iostream>

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
    char *buffer;

    IP() {
        fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
        if (fd == -1) {
            throw IP_ERR;
        }
        buffer = new char[1024 * 32];

        int one = 1;
        const int *val = &one;
        if (setsockopt (fd, IPPROTO_IP, IP_HDRINCL, val, sizeof (one)) < 0) {
            throw IP_ERR;
        }
    }

    ~IP() {
        delete [] buffer;
        close(fd);
    }

    IpHeader *getNextIpPacket(int &length) {
        length = recv(fd, buffer, 1024 * 32, 0);
        return (IpHeader *) buffer;
    }

    void writeIpPacket(IpHeader *ipHeader, int length, uint16_t hostDestPort, uint32_t networkDestIp) {
        sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(hostDestPort);
        sin.sin_addr.s_addr = networkDestIp;

        sendto(fd, ipHeader, length, 0, (sockaddr *) &sin, sizeof(sin));
    }
};

#endif // IP_H
