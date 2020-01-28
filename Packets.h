#ifndef PACKETS_H
#define PACKETS_H

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

#define IpHeader struct iphdr

inline int IpHeader_getVersion(IpHeader *ip) {
    return ip->version;
}

inline int IpHeader_getHeaderLength(IpHeader *ip) {
    return ip->ihl * 4;
}

inline void *IpHeader_getData(IpHeader *ip) {
    return ((char *) ip) + IpHeader_getHeaderLength(ip);
}

// fel!
inline int IpHeader_getTotalLength(IpHeader *ip) {
    return ntohs(ip->tot_len);
}

inline int IpHeader_getFragmentOffset(IpHeader *ip) {
    return ntohs(ip->frag_off);
}

//#define TcpHeader struct tcphdr

struct TcpHeader {
    struct tcphdr header;

    unsigned char options[4];
};

#include <stdint.h>

inline uint16_t TcpHeader_getDestinationPort(struct TcpHeader *tcp) {
    return ntohs(tcp->header.dest);
}

inline uint16_t TcpHeader_getSourcePort(struct TcpHeader *tcp) {
    return ntohs(tcp->header.source);
}

#endif // PACKETS_H
