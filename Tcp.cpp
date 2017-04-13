#include "Tcp.h"

void Socket::sendPacket(uint32_t hostSeq, uint32_t hostAck, uint32_t networkDestIp, uint32_t networkSourceIp, int hostDestPort,
                       int hostSourcePort, bool flagAck, bool flagSyn, bool flagFin, bool flagRst, char *data, size_t length) {

    IpHeader *ipHeader = globalIP->getIpPacketBuffer();
    memset(ipHeader, 0, sizeof(iphdr));

    ipHeader->ihl = 5;
    ipHeader->version = 4;
    ipHeader->tot_len = htons(sizeof(iphdr) + sizeof(TcpHeader) + length);
    ipHeader->id = htonl(54321);
    ipHeader->ttl = 255;
    ipHeader->protocol = IPPROTO_TCP;
    ipHeader->saddr = networkSourceIp;
    ipHeader->daddr = networkDestIp;
    //ipHeader->check = csum_continue(0, (char *) ipHeader, sizeof(iphdr));

    TcpHeader *tcpHeader = (TcpHeader *) ipHeader->getData();
    memset(tcpHeader, 0, sizeof(TcpHeader));

    tcpHeader->ack = flagAck;
    tcpHeader->syn = flagSyn;
    tcpHeader->fin = flagFin;
    tcpHeader->rst = flagRst;
    if (data) {
        tcpHeader->psh = true;
        memcpy(((char *) tcpHeader) + sizeof(TcpHeader), data, length);
    }

    tcpHeader->ack_seq = htonl(hostAck);
    tcpHeader->seq = htonl(hostSeq);
    tcpHeader->source = htons(hostSourcePort);
    tcpHeader->dest = htons(hostDestPort);

    // window scale 512kb / 2
    tcpHeader->options[0] = 3;
    tcpHeader->options[1] = 3;
    tcpHeader->options[2] = 5; // shift
    tcpHeader->options[3] = 0;

    // todo
    tcpHeader->doff = 6; // 5 * 4 = 20 bytes
    tcpHeader->window = htons(8192);

    tcpHeader->check = csum_continue(getPseudoHeaderSum(networkSourceIp, networkDestIp, htons(sizeof(TcpHeader) + length))
                                     , (char *) tcpHeader, sizeof(TcpHeader) + length);
}

// networkAddress, hostPort
std::pair<uint32_t, unsigned int> networkAddressFromString(char *address) {
    unsigned int addr[5];
    sscanf(address, "%d.%d.%d.%d:%d", &addr[0], &addr[1], &addr[2], &addr[3], &addr[4]);

    uint32_t networkAddress = addr[0] << 24 | addr[1] << 16 | addr[2] << 8 | addr[3];
    return {htonl(networkAddress), addr[4]};
}

void Context::connect(char *source, char *destination, void *userData)
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

#include <chrono>

void Context::dispatch(IpHeader *ipHeader, TcpHeader *tcpHeader, unsigned int length) {

    // lookup associated socket
    Endpoint endpoint = {ipHeader->saddr, ntohs(tcpHeader->source), ipHeader->daddr, ntohs(tcpHeader->dest)};
    auto it = sockets.find(endpoint);

    if (it != sockets.end()) {
        Socket *socket = it->second;

        if (tcpHeader->fin) {

            socketStates[socket->applicationState].ondisconnection(socket);
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

                        return;

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

                    socket->originalHostAck = socket->hostAck;

                    Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
                    onconnection(socket);

                    return;

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

            // how big can an IP packet be?
            if (buf + tcpdatalen - (char *) ipHeader > length) {
                std::cout << "ERROR: lengths mismatch!" << std::endl;
                std::cout << "tcpdatalen: " << tcpdatalen << std::endl;
                std::cout << "ip total length: " << ipHeader->getTotalLength() << std::endl;
                std::cout << "buffer length: " << length << std::endl;
                //exit(-1);
            }

            // determine cancer mode or not

            // is this segment out of sequence?
            uint32_t seq = ntohl(tcpHeader->seq);
            //static std::chrono::system_clock::time_point then;
            if (socket->hostAck != seq) {

                if (socket->hostAck > seq) {
                    // already seen data, ignore
                    //std::cout << "GOT DUPLICATE!" << std::endl;

                    // if this is sent, then it melts down completely
                    //Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
                    return;
                }

                /*if (!expectingSeq) {
                    expectingSeq = socket->hostAck;
                    std::cout << "Expecting package with seq: " << expectingSeq << std::endl << std::endl;
                    then = std::chrono::system_clock::now();
                }*/

                // buffer this out of seq segment
                Socket::Block block;
                block.seqStart = seq;
                block.buffer.append(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);
                socket->blocks.push_back(block);

                // send dup ack
                Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
            } else {

                socket->hostAck += tcpdatalen;
                uint32_t lastHostSeq = socket->hostSeq;
                socketStates[socket->applicationState].ondata(socket, buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);

                int blockedSegments = socket->blocks.size();

                //if (socket->blocks.size()) {
                    //int ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - then).count();
                    //std::cout << ms << "ms, Received outstanding package with seq: " << expectingSeq << std::endl;

                restart:
                for (auto it = socket->blocks.begin(); it != socket->blocks.end(); ) {
                    if (socket->hostAck == it->seqStart) {

                        // handle data
                        socket->hostAck += it->buffer.length();
                        socketStates[socket->applicationState].ondata(socket, (char *) it->buffer.data(), it->buffer.length());

                        // kan inte bara fortsätta, måste börja om!
                        it = socket->blocks.erase(it);
                        goto restart;
                    } else {
                        it++;
                    }
                }

                if (socket->blocks.size() != blockedSegments) {
                    std::cout << "Released blocked up segments to application, previously " << blockedSegments << ", now only " << socket->blocks.size() << std::endl;
                    blockedSegments = socket->blocks.size();
                }

                if (lastHostSeq == socket->hostSeq) {
                    Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
                }

                // exra check: make sure to remove buffers that have already been acked!
                for (auto it = socket->blocks.begin(); it != socket->blocks.end(); ) {
                    if (socket->hostAck >= it->seqStart + it->buffer.length()) {
                        it = socket->blocks.erase(it);
                    } else {
                        it++;
                    }
                }

                if (socket->blocks.size() != blockedSegments) {
                    //std::cout << "Cut away already acked blocked segments, previously " << blockedSegments << ", now only " << socket->blocks.size() << std::endl;
                }
            }
        } else {
            std::cout << "We got some packet, maybe ack, with no data, tcp-keep alive?" << std::endl;
        }
    } else if (tcpHeader->syn && !tcpHeader->ack) {
        uint32_t hostAck = ntohl(tcpHeader->seq);
        uint32_t hostSeq = rand();
        sockets[endpoint] = new Socket({nullptr, ipHeader->saddr, tcpHeader->getSourcePort(), ipHeader->daddr, tcpHeader->getDestinationPort(), hostAck, hostSeq, Socket::SYN_ACK_SENT});
        Socket::sendPacket(hostSeq, hostAck + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, true, false, false, nullptr, 0);
    } else {
        std::cout << "Dropping uninvited packet" << std::endl;
    }
}

IP *globalIP;
