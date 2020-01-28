// socket context implements tcp protcol

/* uSockets is entierly opaque so we can use the real header straight up */
#include "../uWebSockets.js/uWebSockets/uSockets/src/libusockets.h"

#include "internal.h"


// socket container with lookup

#define SOCKET_SYN_ACK_SENT 1
#define SOCKET_ESTABLISHED 2
#define SOCKET_SYN_SENT 3

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

IpHeader *getIpPacketBuffer(struct us_loop_t *loop);

// needs to know of the loop (IP)
void us_internal_socket_context_send_packet(struct us_socket_context_t *context, uint32_t hostSeq, uint32_t hostAck, uint32_t networkDestIp, uint32_t networkSourceIp, int hostDestPort,
                 int hostSourcePort, int flagAck, int flagSyn, int flagFin, int flagRst, char *data, size_t length) {

    struct us_loop_t *loop = context->loop;

    IpHeader *ipHeader = getIpPacketBuffer(loop);

    memset(ipHeader, 0, sizeof(struct iphdr));

    ipHeader->ihl = 5;
    ipHeader->version = 4;
    ipHeader->tot_len = htons(sizeof(struct iphdr) + sizeof(struct TcpHeader) + length);
    ipHeader->id = htonl(54321);
    ipHeader->ttl = 255;
    ipHeader->protocol = IPPROTO_TCP;
    ipHeader->saddr = networkSourceIp;
    ipHeader->daddr = networkDestIp;
    //ipHeader->check = csum_continue(0, (char *) ipHeader, sizeof(iphdr));

    struct TcpHeader *tcpHeader = (struct TcpHeader *) IpHeader_getData(ipHeader);//ipHeader->getData();
    memset(tcpHeader, 0, sizeof(struct TcpHeader));

    tcpHeader->header.ack = flagAck;
    tcpHeader->header.syn = flagSyn;
    tcpHeader->header.fin = flagFin;
    tcpHeader->header.rst = flagRst;
    if (data) {
        tcpHeader->header.psh = 1;
        memcpy(((char *) tcpHeader) + sizeof(struct TcpHeader), data, length);
    }

    tcpHeader->header.ack_seq = htonl(hostAck);
    tcpHeader->header.seq = htonl(hostSeq);
    tcpHeader->header.source = htons(hostSourcePort);
    tcpHeader->header.dest = htons(hostDestPort);

    // window scale 512kb / 2
    tcpHeader->options[0] = 3;
    tcpHeader->options[1] = 3;
    tcpHeader->options[2] = 5; // shift
    tcpHeader->options[3] = 0;

    // todo
    tcpHeader->header.doff = 6; // 5 * 4 = 20 bytes
    tcpHeader->header.window = htons(8192);

    tcpHeader->header.check = csum_continue(getPseudoHeaderSum(networkSourceIp, networkDestIp, htons(sizeof(struct TcpHeader) + length))
                                     , (char *) tcpHeader, sizeof(struct TcpHeader) + length);
}

// this is the only socket we can keep for now
struct us_socket_t *s;

// looking up a socket from its properties
struct us_socket_t *lookup_socket(uint32_t sourceIP, uint16_t sourcePort, uint32_t destIP, uint16_t destPort) {
    return s;
}

// creates a new socket with the given description
struct us_socket_t *add_socket() {
    s = malloc(sizeof(struct us_socket_t));
    // init the socket

    s->closed = 0;
    s->shutdown = 0;
    return s;
}

void remove_socket(struct us_socket_t *ss) {
    free(ss);

    s = 0;
}

//us_socket_write can take identifier to merge common sends into one


// pass tcp data to the context - call it read_packet, send_packet
void us_internal_socket_context_read_tcp(struct us_socket_context_t *context, IpHeader *ipHeader, struct TcpHeader *tcpHeader, int length) {

    // lookup socket?
    struct us_socket_t *s = lookup_socket(ipHeader->saddr, ntohs(tcpHeader->header.source), ipHeader->daddr, ntohs(tcpHeader->header.dest));
    // this socket did not even exist!
    if (!s) {
        // is this a syn or ack?
        if (tcpHeader->header.syn && !tcpHeader->header.ack) {

                uint32_t hostAck = ntohl(tcpHeader->header.seq);
                uint32_t hostSeq = rand();

                struct us_socket_t *s = add_socket();

                s->state = SOCKET_SYN_ACK_SENT;
                s->context = context;
                s->hostAck = hostAck;
                s->hostSeq = hostSeq;

                s->networkIp = ipHeader->saddr;
                s->hostPort = TcpHeader_getSourcePort(tcpHeader);

                s->networkDestinationIp = ipHeader->daddr;
                s->hostDestinationPort = TcpHeader_getDestinationPort(tcpHeader);


                us_internal_socket_context_send_packet(context, hostSeq, hostAck + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 1, 0, 0, NULL, 0);

                // give away their ip to callback
                //context->on_open(s, 1, &s->networkIp, 4);
        } else {
            //std::cout << "Dropping uninvited packet" << std::endl;

            printf("Dropping uninvited packet\n");
        }


    } else {
        // we do have this socket already

        if (tcpHeader->header.fin) {

            // emit callback
            context->on_close(s);
            remove_socket(s);

            // send fin, ack back
            s->hostAck += 1;


            us_internal_socket_context_send_packet(context, s->hostSeq, s->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 0, 1, 0, NULL, 0);


        } else if (tcpHeader->header.ack) {

            if (s->state == SOCKET_ESTABLISHED) {
                // why thank you
            } else if (s->state == SOCKET_SYN_ACK_SENT) {

                // note: PSH, ACK with data can act as connection ACK (this is probably good)
                // let's just not allow it for now
                if (!tcpHeader->header.psh) {

                    uint32_t seq = ntohl(tcpHeader->header.seq);
                    uint32_t ack = ntohl(tcpHeader->header.ack_seq);

                    if (s->hostAck + 1 == seq && s->hostSeq + 1 == ack) {
                        s->hostAck++;
                        s->hostSeq++;
                        s->state = SOCKET_ESTABLISHED;

                        //onconnection(socket);

                        context->on_open(s, 1, 0, 0); // I guess this is for outbound connections

                        return;

                        // empty eventual buffering hindered so far by the lower seq and ack numbers!

                    } else {
                        //std::cout << "Server ACK is wrong!" << std::endl;
                    }

                }

            } else if (tcpHeader->header.syn && s->state == SOCKET_SYN_SENT) {

                uint32_t ack = ntohl(tcpHeader->header.ack_seq);

                if (s->hostSeq + 1 == ack) {

                    uint32_t seq = ntohl(tcpHeader->header.seq);
                    s->hostSeq++;
                    s->hostAck = seq + 1;
                    s->state = SOCKET_ESTABLISHED;

                    s->originalHostAck = s->hostAck;

                    us_internal_socket_context_send_packet(context, s->hostSeq, s->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 0, 0, 0, NULL, 0);


                    //onconnection(socket);

                    context->on_open(s, 0, 0, 0);

                    return;

                    // can data be ready to dispatch here? Don't think so?
                } else {
                    printf("client ack is wrong!\n");
                    //std::cout << "CLIENT ACK IS WRONG!" << std::endl;
                }
            }
        }

        // data handler
        int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->header.doff * 4) - (ipHeader->ihl * 4);
        if (tcpdatalen) {
            char *buf = (char *) ipHeader;

            // how big can an IP packet be?
            if (buf + tcpdatalen - (char *) ipHeader > length) {
                printf("ERROR! length mismatch!\n");
                //std::cout << "ERROR: lengths mismatch!" << std::endl;
                //std::cout << "tcpdatalen: " << tcpdatalen << std::endl;
                //std::cout << "ip total length: " << ipHeader->getTotalLength() << std::endl;
                //std::cout << "buffer length: " << length << std::endl;
                //exit(-1);
            }

            // determine cancer mode or not

            // is this segment out of sequence?
            uint32_t seq = ntohl(tcpHeader->header.seq);
            //static std::chrono::system_clock::time_point then;
            if (s->hostAck != seq) {

                printf("we are in cancer mode! out of sync!\n");

                if (s->hostAck > seq) {
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
     //           Socket::Block block;
      //          block.seqStart = seq;
       //         block.buffer.append(buf + ipHeader->ihl * 4 + tcpHeader->doff * 4, tcpdatalen);
       //         socket->blocks.push_back(block);

                // send dup ack
       //         Socket::sendPacket(socket->hostSeq, socket->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->source), ntohs(tcpHeader->dest), true, false, false, false, nullptr, 0);
            } else {

                s->hostAck += tcpdatalen;
                uint32_t lastHostSeq = s->hostSeq;

                // emit data
                context->on_data(s, buf + ipHeader->ihl * 4 + tcpHeader->header.doff * 4, tcpdatalen);

                //int blockedSegments = socket->blocks.size();

                //if (socket->blocks.size()) {
                    //int ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - then).count();
                    //std::cout << ms << "ms, Received outstanding package with seq: " << expectingSeq << std::endl;

//                restart:
//                for (auto it = socket->blocks.begin(); it != socket->blocks.end(); ) {
//                    if (socket->hostAck == it->seqStart) {

//                        // handle data
//                        socket->hostAck += it->buffer.length();
//                        socketStates[socket->applicationState].ondata(socket, (char *) it->buffer.data(), it->buffer.length());

//                        // kan inte bara fortsätta, måste börja om!
//                        it = socket->blocks.erase(it);
//                        goto restart;
//                    } else {
//                        it++;
//                    }
//                }

//                if (socket->blocks.size() != blockedSegments) {
//                    std::cout << "Released blocked up segments to application, previously " << blockedSegments << ", now only " << socket->blocks.size() << std::endl;
//                    blockedSegments = socket->blocks.size();
//                }

                if (lastHostSeq == s->hostSeq) {
                    us_internal_socket_context_send_packet(context, s->hostSeq, s->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 0, 0, 0, NULL, 0);
                }

//                // exra check: make sure to remove buffers that have already been acked!
//                for (auto it = socket->blocks.begin(); it != socket->blocks.end(); ) {
//                    if (socket->hostAck >= it->seqStart + it->buffer.length()) {
//                        it = socket->blocks.erase(it);
//                    } else {
//                        it++;
//                    }
//                }

//                if (socket->blocks.size() != blockedSegments) {
//                    //std::cout << "Cut away already acked blocked segments, previously " << blockedSegments << ", now only " << socket->blocks.size() << std::endl;
//                }
            }
        } else {
            printf("We got some packet, maybe ack, with no data, tcp-keep alive?\n");
        }

    }
}


struct us_socket_context_t *us_create_socket_context(int ssl, struct us_loop_t *loop, int ext_size, struct us_socket_context_options_t options) {
    struct us_socket_context_t *socket_context = (struct us_socket_context_t *) malloc(sizeof(struct us_socket_context_t) + ext_size);

    socket_context->loop = loop;

    // länka ihop socket contextet med loopen
    loop->context = socket_context;

    //printf("us_create_socket_context: %p\n", socket_context);

    return socket_context;
}

void us_socket_context_free(int ssl, struct us_socket_context_t *context) {
    //printf("us_socket_context_free: %p\n", context);
    free(context);
}

void us_socket_context_on_open(int ssl, struct us_socket_context_t *context, struct us_socket_t *(*on_open)(struct us_socket_t *s, int is_client, char *ip, int ip_length)) {
    context->on_open = on_open;
}

void us_socket_context_on_close(int ssl, struct us_socket_context_t *context, struct us_socket_t *(*on_close)(struct us_socket_t *s)) {
    context->on_close = on_close;
}

void us_socket_context_on_data(int ssl, struct us_socket_context_t *context, struct us_socket_t *(*on_data)(struct us_socket_t *s, char *data, int length)) {
    context->on_data = on_data;
}

void us_socket_context_on_writable(int ssl, struct us_socket_context_t *context, struct us_socket_t *(*on_writable)(struct us_socket_t *s)) {
    context->on_writable = on_writable;
}

void us_socket_context_on_timeout(int ssl, struct us_socket_context_t *context, struct us_socket_t *(*on_timeout)(struct us_socket_t *s)) {
    context->on_timeout = on_timeout;
}

void us_socket_context_on_end(int ssl, struct us_socket_context_t *context, struct us_socket_t *(*on_end)(struct us_socket_t *s)) {
    context->on_end = on_end;
}

void *us_socket_context_ext(int ssl, struct us_socket_context_t *context) {
    return context + 1;
}

struct us_listen_socket_t {
    int socket_ext_size;
    struct us_socket_context_t *context;
};

struct us_listen_socket_t *us_socket_context_listen(int ssl, struct us_socket_context_t *context, const char *host, int port, int options, int socket_ext_size) {
    struct us_listen_socket_t *listen_socket = (struct us_listen_socket_t *) malloc(sizeof(struct us_listen_socket_t));

    listen_socket->socket_ext_size = socket_ext_size;
    listen_socket->context = context;

    //context->loop->listen_socket = listen_socket;

    return listen_socket;
}

void us_listen_socket_close(int ssl, struct us_listen_socket_t *ls) {
    free(ls);
}

struct us_socket_t *us_socket_context_connect(int ssl, struct us_socket_context_t *context, const char *host, int port, int options, int socket_ext_size) {
    //printf("us_socket_context_connect\n");

    return 0;
}

struct us_loop_t *us_socket_context_loop(int ssl, struct us_socket_context_t *context) {
    return context->loop;
}

struct us_socket_t *us_socket_context_adopt_socket(int ssl, struct us_socket_context_t *context, struct us_socket_t *s, int ext_size) {
    struct us_socket_t *new_s = (struct us_socket_t *) realloc(s, sizeof(struct us_socket_t) + ext_size);
    new_s->context = context;

    return new_s;
}

struct us_socket_context_t *us_create_child_socket_context(int ssl, struct us_socket_context_t *context, int context_ext_size) {
    /* We simply create a new context in this mock */
    struct us_socket_context_options_t options = {};
    struct us_socket_context_t *child_context = us_create_socket_context(ssl, context->loop, context_ext_size, options);

    return child_context;
}
