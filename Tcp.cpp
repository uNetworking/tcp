#include "Tcp.h"

void Tcp::dispatch(IpHeader *ipHeader, TcpHeader *tcpHeader) {

    // connection begin handler
    if (tcpHeader->syn) {

        int hostSeq = rand();
        inSynAckState.insert(htonl(hostSeq));

        Socket::sendPacket(hostSeq, ntohl(tcpHeader->seq) + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, true, false, false, nullptr, 0);

        // disconnection handler
    } else if (tcpHeader->fin) {

        // more identification needed here
        Endpoint endpoint = {ipHeader->saddr, tcpHeader->source};

        if (sockets.find(endpoint) != sockets.end()) {
            ondisconnection(sockets[endpoint]);

            sockets.erase(endpoint);
        } else {
            std::cout << "FIN for already closed socket!" << std::endl;
        }

        // this is not right

        //std::cout << "Sending FIN-ACK with seq=" << /*ntohl(tcpHeader->ack)*/(seq + 1) << ", ack= " << (ntohl(tcpHeader->seq) + 1) << std::endl;
        //Socket::sendPacket(/*ntohl(tcpHeader->ack)*/ htonl(seq + 1), ntohl(tcpHeader->seq) + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, true);


        // connection complete handler
    } else if (tcpHeader->ack) {

        // store ack and seq (tvärt om för oss)
        uint32_t ack = ntohl(tcpHeader->seq);
        uint32_t seq = ntohl(tcpHeader->ack_seq);

        // map from ip and port to ack and seq

        if (inSynAckState.find(htonl((seq - 1))) != inSynAckState.end()) {

            Socket *socket = new Socket({ipHeader->saddr, tcpHeader->getSourcePort(), ack, seq});

            Endpoint endpoint = {ipHeader->saddr, tcpHeader->source};
            sockets[endpoint] = socket;

            onconnection(socket);

            inSynAckState.erase(htonl((seq - 1)));
        } else {
            // ack, psh?
        }
    }

    // data handler
    int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->doff * 4) - (ipHeader->ihl * 4);
    if (tcpdatalen) {

        // mappa fram ack och seq!
        Endpoint endpoint = {ipHeader->saddr, tcpHeader->source};
        Socket *socket = sockets[endpoint];

        char *buf = (char *) ipHeader;

        socket->hostAck += tcpdatalen;

        // we can send here?
        //ondata(socket, buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);


        Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);

        // we can send here?
        ondata(socket, buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);
    }
}

IP globalIP;
