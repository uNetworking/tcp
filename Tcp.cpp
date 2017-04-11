#include "Tcp.h"

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

            // drop packets randomly (sender should take care of this!)
            if (rand() % 100 < 5) {
                std::cout << "Dropping packet for fun" << std::endl;
                return;
            }

            // is this segment out of sequence?
            if (socket->hostAck != ntohl(tcpHeader->seq)) {
                // drop everything wrong!
                if (socket->hostAck > ntohl(tcpHeader->seq)) {
                    // this can be a sign of an ACK we sent not being properly received
                    while (true) {
                        std::cout << "Dropping duplicate packet!" << std::endl;
                        usleep(100);
                    }
                    return;
                } else {
                    std::cout << "Dropping out of sequence packet!" << std::endl;
                    return;
                }
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
