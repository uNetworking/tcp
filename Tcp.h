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

unsigned short csum(unsigned short *ptr,int nbytes)
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

unsigned short getChecksum(tcphdr *tcpHeader, pseudo_header *info, char *data = nullptr, size_t length = 0) {
    char buf[sizeof(tcphdr) + sizeof(pseudo_header) + 1024];
    memcpy(buf + sizeof(pseudo_header), tcpHeader, sizeof(tcphdr));
    memcpy(buf, info, sizeof(pseudo_header));

    memcpy(buf + sizeof(pseudo_header) + sizeof(pseudo_header), data, length);

    return csum((unsigned short *) buf, sizeof(tcphdr) + sizeof(pseudo_header) + length);
}

#include <set>

struct Tcp {
    std::set<uint32_t> inSynAckState;

    void run() {
        int s = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
        if (s == -1) {
            std::cout << "Cannot listen, run as root!" << std::endl;
            exit(-1);
            //return -1;
        }

        char buf[1024];

        while (true) {
            int length = recv(s, buf, 1024, 0);


            iphdr *ipHeader = (iphdr *) buf;
            /*std::cout << "IP version: " << ipHeader->version << std::endl;
            std::cout << "Source addr: "; print_ip(ipHeader->saddr);
            std::cout << "Dest addr: "; print_ip(ipHeader->daddr);*/
            //std::cout << "ihl: " << ipHeader->ihl << std::endl;

            tcphdr *tcpHeader = (tcphdr *) (buf + ipHeader->ihl * 4);
            if (ntohs(tcpHeader->dest) == 3000 || ntohs(tcpHeader->source) == 3000) {

                // client sent server something
                if (ntohs(tcpHeader->dest) == 3000) {
                    //std::cout << "Client sent server:" << std::endl;

                    if (tcpHeader->syn) {
    //                    std::cout << "SYN" << std::endl;
    //                    std::cout << "SEQUENCE NUMBER: " << ntohl(tcpHeader->seq) << std::endl;

                        int destPort = ntohs(tcpHeader->source);

                        sockaddr_in sin;
                        sin.sin_family = AF_INET;
                        sin.sin_port = htons(destPort);
                        sin.sin_addr.s_addr = ipHeader->saddr;

                        tcphdr newTcpHeader = {};
                        newTcpHeader.ack = true;
                        newTcpHeader.syn = true;

                        newTcpHeader.ack_seq = htonl(ntohl(tcpHeader->seq) + 1);
                        newTcpHeader.seq = htonl(rand());

                        // add this random seq
                        inSynAckState.insert(newTcpHeader.seq);

                        newTcpHeader.source = tcpHeader->dest;
                        newTcpHeader.doff = 5;
                        newTcpHeader.dest = tcpHeader->source;

                        newTcpHeader.window = htons(43690);

                        // properly calculate checksum for this header
                        pseudo_header info;
                        info.dest_address = sin.sin_addr.s_addr;
                        info.source_address = ipHeader->saddr;
                        info.placeholder = 0;
                        info.protocol = IPPROTO_TCP;
                        info.tcp_length = htons(sizeof(tcphdr));
                        newTcpHeader.check = getChecksum(&newTcpHeader, &info);

                        sendto(s, &newTcpHeader, sizeof(newTcpHeader), 0, (sockaddr *) &sin, sizeof(sin));


                    } else if (tcpHeader->ack) {
    //                    std::cout << "ACK" << std::endl;
    //                    std::cout << "SEQUENCE NUMBER: " << ntohl(tcpHeader->seq) << std::endl;
    //                    std::cout << "ACK_SEQUENCE NUMBER: " << ntohl(tcpHeader->ack_seq) << std::endl;

                        if (inSynAckState.find(htonl((ntohl(tcpHeader->ack_seq) - 1))) != inSynAckState.end()) {


                            onconnection();

                            //std::cout << "Connections: " << ++connections << std::endl;

                            inSynAckState.erase(htonl((ntohl(tcpHeader->ack_seq) - 1)));
                        }
                    }

                } else {
                    //std::cout << "Server sent client: " << std::endl;

                }

                //std::cout << "Source: " << ntohs(tcpHeader->source) << std::endl;
                //std::cout << "Destination: " << ntohs(tcpHeader->dest) << std::endl;

                int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->doff * 4) - (ipHeader->ihl * 4);
                if (tcpdatalen) {

                    // todo: make it work

                    std::cout << "isSyn: " << tcpHeader->syn << std::endl;
                    std::cout << "isRst: " << tcpHeader->rst << std::endl;
                    std::cout << "isFin: " << tcpHeader->fin << std::endl;
                    std::cout << "isAck: " << tcpHeader->ack << std::endl;

                    //std::cout << "<" << std::string(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen) << ">" << std::endl;

                    ondata(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);


                    // send ack with window



                    int destPort = ntohs(tcpHeader->source);

                    sockaddr_in sin;
                    sin.sin_family = AF_INET;
                    sin.sin_port = htons(destPort);
                    sin.sin_addr.s_addr = ipHeader->saddr;

                    tcphdr newTcpHeader = {};
                    newTcpHeader.ack = true;
                    //newTcpHeader.syn = true;

                    //newTcpHeader.ack_seq = htonl(ntohl(tcpHeader->seq) + 1);
                    //newTcpHeader.seq = htonl(rand());

                    // I guess this is the ID
                    newTcpHeader.seq = tcpHeader->seq;

                    newTcpHeader.source = tcpHeader->dest;
                    newTcpHeader.doff = 5;
                    newTcpHeader.dest = tcpHeader->source;

                    // how big our buffer is?
                    newTcpHeader.window = htons(43690);

                    // properly calculate checksum for this header
                    pseudo_header info;
                    info.dest_address = sin.sin_addr.s_addr;
                    info.source_address = ipHeader->saddr;
                    info.placeholder = 0;
                    info.protocol = IPPROTO_TCP;
                    info.tcp_length = htons(sizeof(tcphdr));

                    std::string data(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);

                    newTcpHeader.check = getChecksum(&newTcpHeader, &info, (char *) data.data(), data.length());

                    sendto(s, &newTcpHeader, sizeof(newTcpHeader), 0, (sockaddr *) &sin, sizeof(sin));

                }

                //std::cout << std::endl << std::endl;
            }

        }
    }

    std::function<void()> onconnection;
    std::function<void(char *, size_t)> ondata;

    void onConnection(std::function<void()> f) {
        onconnection = f;
    }


    void onData(std::function<void(char *, size_t)> f) {
        ondata = f;
    }
};

#endif // TCP_H
