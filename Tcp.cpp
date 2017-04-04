#include "Tcp.h"

void IP::releasePackageBatch() {

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
}

void Tcp::dispatch(IpHeader *ipHeader, TcpHeader *tcpHeader) {

    // lookup can be improved
    Endpoint endpoint = {ipHeader->saddr, ntohs(tcpHeader->source), ipHeader->daddr};

    // does this connection exist?
    auto it = sockets.find(endpoint);
    Socket *socket = nullptr;
    if (it != sockets.end()) {
        socket = it->second;
    }

    // connection begin handler
    if (tcpHeader->syn) {

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
                Socket *socket = new Socket({nullptr, ipHeader->saddr, tcpHeader->getSourcePort(), ipHeader->daddr, ack, seq});
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
