#include "Tcp.h"

void IP::releasePackageBatch() {

    if (queuedBuffersNum) {
        mmsghdr sendVec[500] = {};
        sockaddr_in sin[500] = {};

        for (int i = 0; i < queuedBuffersNum; i++) {

            IpHeader *ipHeader = (IpHeader *) outBuffer[i];
            TcpHeader *tcpHeader = (TcpHeader *) ipHeader->getData();

            int length = ipHeader->getTotalLength();

            sin[i].sin_family = AF_INET;
            sin[i].sin_port = tcpHeader->dest;
            sin[i].sin_addr.s_addr = ipHeader->daddr;

            messages[i].iov_base = ipHeader;
            messages[i].iov_len = length;

            sendVec[i].msg_hdr.msg_iov = &messages[i];
            sendVec[i].msg_hdr.msg_iovlen = 1;

            sendVec[i].msg_hdr.msg_name = &sin[i];
            sendVec[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);

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
    auto sourceAddress = networkAddressFromString(source);
    auto destinationAddress = networkAddressFromString(destination);

    uint32_t networkClientSeq = rand();
    Socket::sendPacket(networkClientSeq, 0, destinationAddress.first, sourceAddress.first, destinationAddress.second, sourceAddress.second, false, true, false, false, nullptr, 0);
    ip->releasePackageBatch();

    // probably Endpoint?
    inSynState.insert({networkClientSeq, sourceAddress.second});
}

void Tcp::dispatch(IpHeader *ipHeader, TcpHeader *tcpHeader) {

    // lookup can be improved
    Endpoint endpoint = {ipHeader->saddr, ntohs(tcpHeader->source), ipHeader->daddr, ntohs(tcpHeader->dest)};

    // does this connection exist?
    auto it = sockets.find(endpoint);
    Socket *socket = nullptr;
    if (it != sockets.end()) {
        socket = it->second;
    }

    // connection begin handler
    if (tcpHeader->syn) {

        // syn-ack (client is done now)
        if (tcpHeader->ack) {

            std::cout << "CLIENT got SYN-ACK!" << std::endl;

            // assume this is our socket

            uint32_t ack = ntohl(tcpHeader->seq); // received ACK is one more than the SEQ we sent!
            uint32_t seq = ntohl(tcpHeader->ack_seq); // this is the server's ACK!

            Socket::sendPacket(seq, ack + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);

            // create socket here (probably incorrect seq and ack )

            Socket *socket = new Socket({nullptr, ipHeader->saddr, tcpHeader->getSourcePort(), ipHeader->daddr, tcpHeader->getDestinationPort(), ack + 1, seq});
            sockets[endpoint] = socket;
            onconnection(socket);
            //inSynAckState.erase(htonl((seq - 1)));


        } else {
            // cannot syn already established connection
            if (socket) {
                return;
            }

            // simply answer all syns
            uint32_t hostSeq = rand();
            inSynAckState.insert(htonl(hostSeq));
            Socket::sendPacket(hostSeq, ntohl(tcpHeader->seq) + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, true, false, false, nullptr, 0);

            // no data in syn
            return;
        }
        // disconnection handler
    } else if (tcpHeader->fin) {

        if (!socket) {
            std::cout << "FIN for already closed socket!" << std::endl;
            return;
        }

        ondisconnection(socket);
        sockets.erase(endpoint);

        // send fin, ack back
        socket->hostAck += 1;
        Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, true, false, nullptr, 0);

        // connection complete handler
    } else if (tcpHeader->ack) {

        // if no socket, see if we can establish one!
        if (!socket) {
            // store ack and seq (tvärt om för oss)
            uint32_t ack = ntohl(tcpHeader->seq);
            uint32_t seq = ntohl(tcpHeader->ack_seq);

            // map from ip and port to ack and seq
            if (inSynAckState.find(htonl((seq - 1))) != inSynAckState.end()) {
                Socket *socket = new Socket({nullptr, ipHeader->saddr, tcpHeader->getSourcePort(), ipHeader->daddr, tcpHeader->getDestinationPort(), ack, seq});
                sockets[endpoint] = socket;
                onconnection(socket);
                inSynAckState.erase(htonl((seq - 1)));
            }
        }
    }

    // data handler
    int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->doff * 4) - (ipHeader->ihl * 4);
    if (tcpdatalen) {



        char *buf = (char *) ipHeader;

        if (!socket) {

            // if data before Syn,ACK sent back -> buffer up until?

            // or simply pass it along?


            std::cout << "DATA for already closed socket!" << std::endl;

            std::cout << "DATA: " << std::string(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen) << std::endl;

            return;
        }

        // is this segment out of sequence?
        if (socket->hostAck != ntohl(tcpHeader->seq)) {

            if (socket->hostAck > ntohl(tcpHeader->seq)) {
                std::cout << "INFO: Received duplicate TCP data segment, dropping" << std::endl;
            } else {
                // here we need to buffer up future segments until prior segments come in!

                std::cout << "WARNING: Received out-of-order TCP segment, should buffer but will drop for now" << std::endl;
            }
            return;
        }

        socket->hostAck += tcpdatalen;
        uint32_t lastHostSeq = socket->hostSeq;
        ondata(socket, buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);
        // no data sent, need to send ack!
        if (lastHostSeq == socket->hostSeq) {
            Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
        }
    }
}

IP *globalIP;
