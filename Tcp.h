#ifndef TCP_H
#define TCP_H

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

#include <iostream>
#include <vector>
#include <functional>



#include <set>


// the connection
int ack;
int seq;

int s;

struct Socket {
    static unsigned short csum(unsigned short *ptr,int nbytes)
    {
        register long sum;
        unsigned short oddbyte;
        register short answer;

        sum=0;
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

    struct pseudo_header
    {
        u_int32_t source_address;
        u_int32_t dest_address;
        u_int8_t placeholder;
        u_int8_t protocol;
        u_int16_t tcp_length;
    };

    static unsigned short getChecksum(tcphdr *tcpHeader, pseudo_header *info, char *data = nullptr, size_t length = 0) {
        char buf[sizeof(tcphdr) + sizeof(pseudo_header) + 1024];
        memcpy(buf + sizeof(pseudo_header), tcpHeader, sizeof(tcphdr));
        memcpy(buf, info, sizeof(pseudo_header));

        memcpy(buf + sizeof(pseudo_header) + sizeof(pseudo_header), data, length);

        return csum((unsigned short *) buf, sizeof(tcphdr) + sizeof(pseudo_header) + length);
    }


    static unsigned short getChecksum(iphdr *tcpHeader, pseudo_header *info, char *data = nullptr, size_t length = 0) {
        char buf[sizeof(iphdr) + sizeof(pseudo_header) + 1024];
        memcpy(buf + sizeof(pseudo_header), tcpHeader, sizeof(iphdr));
        memcpy(buf, info, sizeof(pseudo_header));

        memcpy(buf + sizeof(pseudo_header) + sizeof(pseudo_header), data, length);

        return csum((unsigned short *) buf, sizeof(iphdr) + sizeof(pseudo_header) + length);
    }

    static void sendPacket(uint32_t hostSeq, uint32_t hostAck, uint32_t networkDestIp, uint32_t networkSourceIp, int hostDestPort, int hostSourcePort, bool flagAck, bool flagSyn, bool flagFin) {

        // has to include ip header to allow sending from (source) other than 127.0.0.1
        iphdr iph = {};
        iph.ihl = 5;
        iph.version = 4;
        iph.tot_len = sizeof(iphdr) + sizeof(tcphdr);
        iph.id = htonl(54321);
        iph.ttl = 255;
        iph.protocol = IPPROTO_TCP;
        iph.saddr = networkSourceIp;
        iph.daddr = networkDestIp;
        iph.check = csum ((unsigned short *) &iph, sizeof(iphdr));




        // take a copy as base!
        tcphdr newTcpHeader = {};

        // these are flags
        newTcpHeader.ack = flagAck;
        newTcpHeader.syn = flagSyn;
        newTcpHeader.fin = flagFin;


        newTcpHeader.ack_seq = htonl(hostAck);
        newTcpHeader.seq = htonl(hostSeq);
        newTcpHeader.source = htons(hostSourcePort);
        newTcpHeader.dest = htons(hostDestPort);

        // todo
        newTcpHeader.doff = 5; // 5 * 4 = 20 bytes
        newTcpHeader.window = htons(43690); // flow control


        // properly calculate checksum for this header
        pseudo_header info;
        info.dest_address = networkDestIp;
        info.source_address = networkSourceIp;
        info.placeholder = 0;
        info.protocol = IPPROTO_TCP;
        info.tcp_length = htons(sizeof(tcphdr));
        newTcpHeader.check = getChecksum(&newTcpHeader, &info);

        // send it
        sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(hostDestPort);
        sin.sin_addr.s_addr = networkDestIp;


        char buf[sizeof(iphdr) + sizeof(tcphdr)];
        memcpy(buf, &iph, sizeof(iphdr));
        memcpy(buf + sizeof(iphdr), &newTcpHeader, sizeof(tcphdr));

        sendto(s, buf, sizeof(iphdr) + sizeof(tcphdr), 0, (sockaddr *) &sin, sizeof(sin));
    }

    // per socket data
    uint32_t networkIp;
    uint16_t hostPort;

    // our! from our side
    int hostAck;
    int hostSeq;
};

struct Endpoint {
    uint32_t networkIp;
    uint16_t hostPort;
};

bool operator<(const Endpoint a, const Endpoint b) {
    return a.hostPort < a.hostPort;
}

struct TcpState {
    int hostAck;
    int hostSeq;
};

#include <map>

std::map<Endpoint, TcpState> sockets;


void print_ip(int ip)
{
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;
    printf("%d.%d.%d.%d\n", bytes[3], bytes[2], bytes[1], bytes[0]);
}

struct Tcp {
    std::set<uint32_t> inSynAckState;

    void run() {
        s = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
        if (s == -1) {
            std::cout << "Cannot listen, run as root!" << std::endl;
            exit(-1);
        }


        //IP_HDRINCL to tell the kernel that headers are included in the packet
        int one = 1;
        const int *val = &one;
        if (setsockopt (s, IPPROTO_IP, IP_HDRINCL, val, sizeof (one)) < 0)
        {
            perror("Error setting IP_HDRINCL");
            exit(0);
        }


        char buf[1024];

        while (true) {
            recv(s, buf, 1024, 0);

            // ip and tcp headers
            iphdr *ipHeader = (iphdr *) buf;
            tcphdr *tcpHeader = (tcphdr *) (buf + ipHeader->ihl * 4);

            // is this our port?
            if (ntohs(tcpHeader->dest) == 4000) {

                if (tcpHeader->syn) {

                    int hostSeq = rand();
                    inSynAckState.insert(htonl(hostSeq));

                    Socket::sendPacket(hostSeq, ntohl(tcpHeader->seq) + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, true, false);

                } else if (tcpHeader->fin) {


                    Endpoint endpoint = {ipHeader->saddr, tcpHeader->source};

                    //sockets[endpoint] = {ack, seq};

                    if (sockets.find(endpoint) != sockets.end()) {
                        ondisconnection(nullptr);

                        sockets.erase(endpoint);
                    } else {
                        std::cout << "FIN for already closed socket!" << std::endl;
                    }




                    // this is not right

                    //std::cout << "Sending FIN-ACK with seq=" << /*ntohl(tcpHeader->ack)*/(seq + 1) << ", ack= " << (ntohl(tcpHeader->seq) + 1) << std::endl;
                    //Socket::sendPacket(/*ntohl(tcpHeader->ack)*/ htonl(seq + 1), ntohl(tcpHeader->seq) + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, true);


                } else if (tcpHeader->ack) {

                    // store ack and seq (tvärt om för oss)
                    ack = ntohl(tcpHeader->seq);
                    seq = ntohl(tcpHeader->ack_seq);

                    // map from ip and port to ack and seq

                    if (inSynAckState.find(htonl((seq - 1))) != inSynAckState.end()) {


                        Endpoint endpoint = {ipHeader->saddr, tcpHeader->source};
                        sockets[endpoint] = {ack, seq};

                        onconnection(nullptr);


                        inSynAckState.erase(htonl((seq - 1)));
                    }
                }

                int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->doff * 4) - (ipHeader->ihl * 4);
                if (tcpdatalen) {
                    ondata(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);
                    ack += tcpdatalen;
                    Socket::sendPacket(seq, ack, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false);
                }
            }
        }
    }

    std::function<void(Socket *)> onconnection;
    std::function<void(char *, size_t)> ondata;
    std::function<void(Socket *)> ondisconnection;

    void onConnection(std::function<void(Socket *)> f) {
        onconnection = f;
    }

    void onDisconnection(std::function<void(Socket *)> f) {
        ondisconnection = f;
    }

    void onData(std::function<void(char *, size_t)> f) {
        ondata = f;
    }
};

#endif // TCP_H
