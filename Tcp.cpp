#include "Tcp.h"

void IP::releasePackageBatch() {

    if (queuedBuffersNum) {
        mmsghdr sendVec[500] = {};
        sockaddr_in sin[500] = {};

        int packages = queuedBuffersNum;

        for (int i = 0; i < queuedBuffersNum; i++) {

            IpHeader *ipHeader = (IpHeader *) outBuffer[i];
            TcpHeader *tcpHeader = (TcpHeader *) ipHeader->getData();

            int length = ipHeader->getTotalLength();

            sin[i].sin_family = AF_INET;
            sin[i].sin_port = tcpHeader->dest;
            sin[i].sin_addr.s_addr = ipHeader->daddr;

            messages[i].iov_base = ipHeader;
            messages[i].iov_len = length;

            // send out of order!
            sendVec[i].msg_hdr.msg_iov = &messages[packages - i - 1];
            sendVec[i].msg_hdr.msg_iovlen = 1;

            sendVec[i].msg_hdr.msg_name = &sin[packages - i - 1];
            sendVec[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);

//            sendVec[i].msg_hdr.msg_iov = &messages[i];
//            sendVec[i].msg_hdr.msg_iovlen = 1;

//            sendVec[i].msg_hdr.msg_name = &sin[i];
//            sendVec[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);

        }

        sendmmsg(fd, sendVec, queuedBuffersNum, 0);
        queuedBuffersNum = 0;

        //std::cout << "Sent now" << std::endl;
    }
}

void Socket::sendPacket(uint32_t hostSeq, uint32_t hostAck, uint32_t networkDestIp, uint32_t networkSourceIp, int hostDestPort,
                       int hostSourcePort, bool flagAck, bool flagSyn, bool flagFin, bool flagRst, char *data, size_t length) {

    IpHeader *ipHeader = globalIP->getIpPacketBuffer();
    memset(ipHeader, 0, sizeof(iphdr));

    ipHeader->ihl = 5;
    ipHeader->version = 4;
    ipHeader->tot_len = htons(sizeof(iphdr) + sizeof(tcphdr) + length);
    ipHeader->id = htonl(54321);
    ipHeader->ttl = 255;
    ipHeader->protocol = IPPROTO_TCP;
    ipHeader->saddr = networkSourceIp;
    ipHeader->daddr = networkDestIp;
    //ipHeader->check = csum_continue(0, (char *) ipHeader, sizeof(iphdr));

    TcpHeader *tcpHeader = (TcpHeader *) ipHeader->getData();
    memset(tcpHeader, 0, sizeof(tcphdr));

    tcpHeader->ack = flagAck;
    tcpHeader->syn = flagSyn;
    tcpHeader->fin = flagFin;
    tcpHeader->rst = flagRst;
    if (data) {
        tcpHeader->psh = true;
        memcpy(((char *) tcpHeader) + sizeof(tcphdr), data, length);
    }

    tcpHeader->ack_seq = htonl(hostAck);
    tcpHeader->seq = htonl(hostSeq);
    tcpHeader->source = htons(hostSourcePort);
    tcpHeader->dest = htons(hostDestPort);

    // todo
    tcpHeader->doff = 5; // 5 * 4 = 20 bytes
    tcpHeader->window = htons(65535);

    tcpHeader->check = csum_continue(getPseudoHeaderSum(networkSourceIp, networkDestIp, htons(sizeof(tcphdr) + length))
                                     , (char *) tcpHeader, sizeof(tcphdr) + length);
}

#include <sstream>

// networkAddress, hostPort
std::pair<uint32_t, unsigned int> networkAddressFromString(char *address) {
    unsigned int addr[5];
    sscanf(address, "%d.%d.%d.%d:%d", &addr[0], &addr[1], &addr[2], &addr[3], &addr[4]);

    uint32_t networkAddress = addr[0] << 24 | addr[1] << 16 | addr[2] << 8 | addr[3];
    return {htonl(networkAddress), addr[4]};
}

void Tcp::connect(char *source, char *destination, void *userData)
{
    // these should return an Endpoint straight up
    auto sourceAddress = networkAddressFromString(source);
    auto destinationAddress = networkAddressFromString(destination);

    Endpoint endpoint = {destinationAddress.first, destinationAddress.second, sourceAddress.first, sourceAddress.second};

    uint32_t hostSeq = rand();
    sockets[endpoint] = new Socket({nullptr, destinationAddress.first, destinationAddress.second, sourceAddress.first, sourceAddress.second, 0, hostSeq, Socket::SYN_SENT});

    Socket::sendPacket(hostSeq, 0, destinationAddress.first, sourceAddress.first, destinationAddress.second, sourceAddress.second, false, true, false, false, nullptr, 0);
    ip->releasePackageBatch();
}

void Tcp::dispatch(IpHeader *ipHeader, TcpHeader *tcpHeader) {

    // lookup associated socket
    Endpoint endpoint = {ipHeader->saddr, ntohs(tcpHeader->source), ipHeader->daddr, ntohs(tcpHeader->dest)};
    auto it = sockets.find(endpoint);

    if (it != sockets.end()) {
        Socket *socket = it->second;

        if (tcpHeader->fin) {

            ondisconnection(socket);
            sockets.erase(endpoint);

            // send fin, ack back
            socket->hostAck += 1;
            Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, true, false, nullptr, 0);


        } else if (tcpHeader->ack) {

            if (socket->state == Socket::ESTABLISHED) {
                // why thank you
            } else if (socket->state == Socket::SYN_ACK_SENT) {

                // note: PSH, ACK with data can act as connection ACK (this is probably good)
                // let's just not allow it for now
                if (!tcpHeader->psh) {

                    uint32_t seq = ntohl(tcpHeader->seq);
                    uint32_t ack = ntohl(tcpHeader->ack_seq);

                    if (socket->hostAck + 1 == seq && socket->hostSeq + 1 == ack) {
                        socket->hostAck++;
                        socket->hostSeq++;
                        socket->state = Socket::ESTABLISHED;

                        onconnection(socket);

                        // empty eventual buffering hindered so far by the lower seq and ack numbers!

                    } else {
                        //std::cout << "Server ACK is wrong!" << std::endl;
                    }

                }

            } else if (tcpHeader->syn && socket->state == Socket::SYN_SENT) {

                uint32_t ack = ntohl(tcpHeader->ack_seq);

                if (socket->hostSeq + 1 == ack) {

                    uint32_t seq = ntohl(tcpHeader->seq);
                    socket->hostSeq++;
                    socket->hostAck = seq + 1;
                    socket->state = Socket::ESTABLISHED;

                    Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
                    onconnection(socket);

                    // can data be ready to dispatch here? Don't think so?
                } else {
                    std::cout << "CLIENT ACK IS WRONG!" << std::endl;
                }
            }
        }

        // data handler
        int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->doff * 4) - (ipHeader->ihl * 4);
        if (tcpdatalen) {

            char *buf = (char *) ipHeader;

            // is this segment out of sequence?
            if (socket->hostAck != ntohl(tcpHeader->seq)) {

                std::cout << socket->hostAck << " != " << ntohl(tcpHeader->seq) << std::endl;

                if (socket->hostAck > ntohl(tcpHeader->seq)) {
                    std::cout << "INFO: Received duplicate TCP data segment, dropping" << std::endl;
                } else {
                    // here we need to buffer up future segments until prior segments come in!

                    std::cout << "WARNING: Received out-of-order TCP segment, should buffer but will drop for now" << std::endl;

                    std::cout << "Data was: " << std::string(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen) << std::endl;
                }
                return;
            }

            if (socket->state != Socket::ESTABLISHED) {
                std::cout << "Data on not established socket" << std::endl;
            }

            socket->hostAck += tcpdatalen;
            uint32_t lastHostSeq = socket->hostSeq;
            ondata(socket, buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);
            // no data sent, need to send ack!
            if (lastHostSeq == socket->hostSeq) {
                Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
            }
        }
    } else if (tcpHeader->syn && !tcpHeader->ack) {
        // client has sent us its initial (random) seq, we take that and store as our ack

        // since we have a lower ack than any data segment, they will be properly queued
        // up until we get client's ack and increase our ack by 1 (and then empty the buffers)

        uint32_t hostAck = ntohl(tcpHeader->seq);
        uint32_t hostSeq = rand();
        sockets[endpoint] = new Socket({nullptr, ipHeader->saddr, tcpHeader->getSourcePort(), ipHeader->daddr, tcpHeader->getDestinationPort(), hostAck, hostSeq, Socket::SYN_ACK_SENT});

        // we send one ack higher than the one we store!
        Socket::sendPacket(hostSeq, hostAck + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, true, false, false, nullptr, 0);
    } else {
        std::cout << "Dropping uninvited packet" << std::endl;
    }
}

IP *globalIP;
