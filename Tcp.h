#ifndef TCP_H
#define TCP_H

#include "IP.h"

#include <iostream>
#include <vector>
#include <functional>
#include <set>
#include <map>

struct TcpHeader : tcphdr {

    uint16_t getDestinationPort() {
        return ntohs(dest);
    }

    uint16_t getSourcePort() {
        return ntohs(source);
    }

};

extern IP *globalIP;

struct Socket {

    static unsigned short csum_continue(unsigned long sumStart, char *p,int nbytes)
    {
        unsigned short *ptr = (unsigned short *) p;

        register long sum;
        unsigned short oddbyte;
        register short answer;

        sum=sumStart;
        while(nbytes>1) {
            sum+=*ptr++;
            nbytes-=2;
        }
        if(nbytes==1) {
            oddbyte=0;
            *((u_char*)&oddbyte)=*(u_char*)ptr;
            sum+=oddbyte;
        }

        sum = (sum>>16)+(sum & 0xffff);
        sum = sum + (sum>>16);
        answer=(short)~sum;

        return(answer);
    }

//    static unsigned short csum(const char *buf, unsigned size) {
//            unsigned long long sum = 0;
//            const unsigned long long *b = (unsigned long long *) buf;

//            unsigned t1, t2;
//            unsigned short t3, t4;

//            /* Main loop - 8 bytes at a time */
//            while (size >= sizeof(unsigned long long))
//            {
//                    unsigned long long s = *b++;
//                    sum += s;
//                    if (sum < s) sum++;
//                    size -= 8;
//            }

//            /* Handle tail less than 8-bytes long */
//            buf = (const char *) b;
//            if (size & 4)
//            {
//                    unsigned s = *(unsigned *)buf;
//                    sum += s;
//                    if (sum < s) sum++;
//                    buf += 4;
//            }

//            if (size & 2)
//            {
//                    unsigned short s = *(unsigned short *) buf;
//                    sum += s;
//                    if (sum < s) sum++;
//                    buf += 2;
//            }

//            if (size)
//            {
//                    unsigned char s = *(unsigned char *) buf;
//                    sum += s;
//                    if (sum < s) sum++;
//            }

//            /* Fold down to 16 bits */
//            t1 = sum;
//            t2 = sum >> 32;
//            t1 += t2;
//            if (t1 < t2) t1++;
//            t3 = t1;
//            t4 = t1 >> 16;
//            t3 += t4;
//            if (t3 < t4) t3++;

//            return ~t3;
//    }

    static unsigned long getPseudoHeaderSum(u_int32_t saddr, u_int32_t daddr, u_int16_t tcpLength) {
        struct PseudoHeader {
            u_int32_t source_address;
            u_int32_t dest_address;
            u_int8_t placeholder;
            u_int8_t protocol;
            u_int16_t tcp_length;
        } volatile pseudoHeader = {saddr, daddr, 0, IPPROTO_TCP, tcpLength};

        unsigned short *ptr = (unsigned short *) &pseudoHeader;
        unsigned long sum = 0;
        for (int i = 0; i < 6; i++) {
            sum += *ptr++;
        }
        return sum;
    }

    static void sendPacket(uint32_t hostSeq, uint32_t hostAck, uint32_t networkDestIp, uint32_t networkSourceIp, int hostDestPort,
                           int hostSourcePort, bool flagAck, bool flagSyn, bool flagFin, bool flagRst, char *data, size_t length);

    void send(char *data, size_t length) {
        sendPacket(hostSeq, hostAck, networkIp, networkDestinationIp, hostPort, hostDestinationPort, true, false, false, false, data, length);
        hostSeq += length;
    }

    // RST (is this blocked by the kernel?)
    void terminate() {
        //sendPacket(hostSeq, hostAck, networkIp, 0, hostPort, 4000, false, false, false, true, nullptr, 0);
    }

    void *userData;

    // per socket data
    uint32_t networkIp; // this is THEIR IP!
    uint16_t hostPort; // this is THEIR port

    // this is OUR IP!
    uint32_t networkDestinationIp;
    uint16_t hostDestinationPort;

    uint32_t hostAck;
    uint32_t hostSeq;
};

struct Endpoint {
    uint32_t networkIp;
    uint16_t hostPort;

    uint32_t networkDestinationIp;
    uint16_t hostDestinationPort;
};

inline bool operator<(const Endpoint a, const Endpoint b) {

    union hasher {
        Endpoint ep;
        __int128 i;
    };

    hasher aH;
    aH.i = 0;
    aH.ep = a;

    hasher bH;
    bH.i = 0;
    bH.ep = b;

    return aH.i < bH.i;
}

struct Tcp {
    // accepting state
    std::set<uint32_t> inSynAckState;

    // connecting state (client seq, client port)
    std::set<std::pair<uint32_t, uint16_t>> inSynState;

    // established
    std::map<Endpoint, Socket *> sockets;

    IP *ip;
    int port;

    Tcp(IP *ip, int port) :ip(ip), port(port) {
        globalIP = ip;
    }

    void connect(char *destination, void *userData);

    void dispatch(IpHeader *ipHeader, TcpHeader *tcpHeader);

    void run() {
        while (true) {
            int messages = ip->fetchPackageBatch();
            for (int i = 0; i < messages; i++) {
                IpHeader *ipHeader = ip->getIpPacket(i);
                TcpHeader *tcpHeader = (TcpHeader *) ipHeader->getData();
                if (tcpHeader->getDestinationPort() == port || tcpHeader->getDestinationPort() == 4001) {
                    dispatch(ipHeader, tcpHeader);
                }
            }
            ip->releasePackageBatch();
        }
    }

    std::function<void(Socket *)> onconnection;
    std::function<void(Socket *, char *, size_t)> ondata;
    std::function<void(Socket *)> ondisconnection;

    void onConnection(std::function<void(Socket *)> f) {
        onconnection = f;
    }

    void onDisconnection(std::function<void(Socket *)> f) {
        ondisconnection = f;
    }

    void onData(std::function<void(Socket *, char *, size_t)> f) {
        ondata = f;
    }
};

#endif // TCP_H
