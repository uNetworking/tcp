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
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>

struct IP {

    int fd, fd_send;
    char *buffer[500];
    size_t length[500];
    mmsghdr msgs[500];
    iovec iovecs[500];

    iovec messages[500];

    char *outBuffer[500];
    int queuedBuffersNum = 0;

    sockaddr_ll device;

    IP() {
        // send (move over to packet socket later)
        fd_send = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

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







        // tcp and ip syn data
        unsigned char ipTcp[] = {0x45, 0x00, 0x00, 0x28, 0x23, 0xc6, 0x00, 0x00, 0x40, 0x06, 0x59, 0x08, 0x7f, 0x00, 0x00, 0x01,
                        0x7f, 0x00, 0x00, 0x01, 0x0f, 0xa1, 0x0b, 0xb8, 0x6b, 0x8b, 0x45, 0x67, 0x00, 0x00, 0x00, 0x00,
                        0x50, 0x02, 0xaa, 0xaa, 0x3a, 0xea, 0x00, 0x00};

        bool asPacket = true;

        if (asPacket) {
            // send first as packet
            fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
            if (fd == -1) {
                throw IP_ERR;
            }

//            ifreq ifr;
//            memset(&ifr, 0, sizeof(ifr));
//            strcpy(ifr.ifr_name, "wlp3s0");
//            if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
//                throw IP_ERR;
//            }

            memset(&device, 0, sizeof(device));
            if (!(device.sll_ifindex = if_nametoindex("wlp3s0"))) {
                throw IP_ERR;
            }
            device.sll_family = AF_PACKET;
            device.sll_protocol = htons(ETH_P_IP);


            // skicka till routern!
            unsigned char dst_mac[6] = {0x08, 0x76, 0xff, 0x87, 0xc7, 0xed};//{0xDC, 0x85, 0xDE, 0x3B, 0x8C, 0x89};

            // destination MAC (nextHop)
            memcpy (device.sll_addr, dst_mac, 6);

            device.sll_halen = 6;

            // source mac borde vara känd


            //std::cout << sendto(fd, ipTcp, 40, 0, (struct sockaddr *) &device, sizeof(device)) << std::endl;

        } else {

            // then send as raw socket
            fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
            if (fd == -1) {
                throw IP_ERR;
            }

            sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_port = htons(3000);
            sin.sin_addr.s_addr = htonl(0x7f000001);
            std::cout << sendto(fd, ipTcp, 40, 0, (struct sockaddr *) &sin, sizeof(sin)) << std::endl;

        }

        //exit(0);

        // my ip 192.168.1.71
        // my hw:


        // sendto with pre-made frame


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
        IpHeader *ipHeader = (IpHeader *) iovecs[index].iov_base;

        if (ipHeader->frag_off && ipHeader->frag_off != IP_DF) {
            std::cout << "FRAGMENTED: " << ipHeader->frag_off << std::endl;
        }

        return ipHeader;
    }

    IpHeader *getIpPacketBuffer() {
        if (queuedBuffersNum == 500) {
            releasePackageBatch();
        }
        return (IpHeader *) outBuffer[queuedBuffersNum++];
    }
};

#endif // IP_H
