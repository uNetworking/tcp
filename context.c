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
struct us_socket_t *global_s;

// looking up a socket from its properties
struct us_socket_t *lookup_socket(uint32_t sourceIP, uint16_t sourcePort, uint32_t destIP, uint16_t destPort) {


    return global_s;
}

// creates a new socket with the given description
struct us_socket_t *add_socket() {
    if (global_s) {
        printf("ERRO! ALREADY HAVING ONE SOCKET!\n");
        exit(1);
    }

    // allokera mer!
    global_s = malloc(sizeof(struct us_socket_t) + /*256*/512);
    // init the socket

    global_s->closed = 0;
    global_s->shutdown = 0;

    return global_s;
}

void remove_socket(struct us_socket_t *s);

//us_socket_write can take identifier to merge common sends into one


// pass tcp data to the context - call it read_packet, send_packet
void us_internal_socket_context_read_tcp(struct us_socket_t *s, struct us_socket_context_t *context, IpHeader *ipHeader, struct TcpHeader *tcpHeader, int length) {

    if (!s) {
        /* Is this a SYN but not and ACK? */
        if (tcpHeader->header.syn && !tcpHeader->header.ack) {
                /* Allocate the socket */
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

                /* Debug */
                s->packets = 1; // obviously we got SYN
                s->mostOutOfSync = 0;

                /* Send syn, ack */
                us_internal_socket_context_send_packet(context, hostSeq, hostAck + 1, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 1, 0, 0, NULL, 0);
        
                /* Now we will return, and global_s is added to the hash table in loop */
        } else {
            /* All other packtes in this state are uninvited */
            printf("Dropping uninvited packet\n");
        }
    } else {

        // SYN allokerar socketen, alla nästkommande paket räknas upp
        s->packets++;

        if (tcpHeader->header.fin) {

            printf("We got fin!\n");

            // emit callback
            context->on_close(s);

            // send fin, ack back
            s->hostAck += 1;

            us_internal_socket_context_send_packet(context, s->hostSeq, s->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 0, 1, 0, NULL, 0);

            printf("Deleting socket now!\n");
            remove_socket(s);

            /* Cannot fall through to data when deleted */
            return;

        } else if (tcpHeader->header.ack) {

            if (s->state == SOCKET_ESTABLISHED) {
                // why thank you
            } else if (s->state == SOCKET_SYN_ACK_SENT) {

                /* Here we are established, and we may also get data */
                uint32_t seq = ntohl(tcpHeader->header.seq);
                uint32_t ack = ntohl(tcpHeader->header.ack_seq);

                if (s->hostAck + 1 == seq && s->hostSeq + 1 == ack) {
                    s->hostAck++;
                    s->hostSeq++;
                    s->state = SOCKET_ESTABLISHED;

                    /* Emit open event */
                    context->on_open(s, 0, "nej!", 0);
                } else {
                    printf("Server ack is wrong!\n");
                    exit(0);
                }

            } else if (tcpHeader->header.syn && s->state == SOCKET_SYN_SENT) {

                // this is for outbound connections

                printf("Socket syn sent!\n");

                uint32_t ack = ntohl(tcpHeader->header.ack_seq);

                if (s->hostSeq + 1 == ack) {

                    uint32_t seq = ntohl(tcpHeader->header.seq);
                    s->hostSeq++;
                    s->hostAck = seq + 1;
                    s->state = SOCKET_ESTABLISHED;

                    s->originalHostAck = s->hostAck;

                    us_internal_socket_context_send_packet(context, s->hostSeq, s->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 0, 0, 0, NULL, 0);

                    context->on_open(s, 0, 0, 0);

                    return;

                    // can data be ready to dispatch here? Don't think so?
                } else {
                    printf("client ack is wrong!\n");
                }
            }
        }

        /* Data handler runs for everything falling through from above */
        int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->header.doff * 4) - (ipHeader->ihl * 4);
        if (tcpdatalen) {
            char *buf = (char *) ipHeader;

            // how big can an IP packet be?
            if (buf + tcpdatalen - (char *) ipHeader > length) {
                printf("ERROR! length mismatch!\n");
                exit(0);
                //std::cout << "ERROR: lengths mismatch!" << std::endl;
                //std::cout << "tcpdatalen: " << tcpdatalen << std::endl;
                //std::cout << "ip total length: " << ipHeader->getTotalLength() << std::endl;
                //std::cout << "buffer length: " << length << std::endl;
                //exit(-1);
            }

            /* Is this segment out of sequence? */
            uint32_t seq = ntohl(tcpHeader->header.seq);
            if (s->hostAck != seq) {

                /* This implementation should rarely see out of orders, since most sends are 1 packet big */
                /* We only throw away future packets, and count on TCP resending them. Duplicates are acked again */

                /* We have already seen this packet, calm down, the client did not get ack in time */
                if (s->hostAck > seq) {

                    context->loop->duplicated_packets++;

                    /* Should probably send a new ack */
                    us_internal_socket_context_send_packet(context, s->hostSeq, s->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 0, 0, 0, NULL, 0);

                    return;

                } else {
                    /* We got a packet that is supposed to be for the future, we need to buffer it up or just throw it away */
                    uint32_t future = (seq - s->hostAck);
                    //printf("We got a packet that is %d bytes in the future as socket's %d packet so far including SYN\n", future, s->packets);


                    // increase for statistics
                    context->loop->packets_out_of_order++;

                    // for now just store one
                    if (!s->mostOutOfSync) {
                        s->mostOutOfSync = seq;
                    }
                    

                    // om en socket som är i fucked state får ett nytt paket som är i fas


                    // we should mark this socket with the highest seen packet, then tell the user when we "healed" from this out of order

                    /* For now we just throw this packet to hell */
                    return;

                    /* This should be buffered up and released when the missing piece reached us */
                    exit(0);
                }
            } else {
                /* Increase by length of data */
                s->hostAck += tcpdatalen;
                uint32_t lastHostSeq = s->hostSeq;

                /* Now that we received data in sync, did we heal the out of sync? */
                if (s->mostOutOfSync) {
                    printf("WE HELATED A SOCKET A BIT!\n");
                    context->loop->healed_sockets++;
                    s->mostOutOfSync = 0;
                }

                /* Emit data, changing socket if necessary */
                s = context->on_data(s, buf + ipHeader->ihl * 4 + tcpHeader->header.doff * 4, tcpdatalen);

                /* If we did not send anything in the callback, send and ack (we're not piggybacking) */
                if (lastHostSeq == s->hostSeq) {
                    us_internal_socket_context_send_packet(context, s->hostSeq, s->hostAck, ipHeader->saddr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 0, 0, 0, NULL, 0);
                }
            }
        } else {
            /* We got something here, ack, or something we don't care about */
        }
    }
}

struct us_socket_context_t *us_create_socket_context(int ssl, struct us_loop_t *loop, int ext_size, struct us_socket_context_options_t options) {
    struct us_socket_context_t *socket_context = (struct us_socket_context_t *) malloc(sizeof(struct us_socket_context_t) + ext_size);

    /* Link socket context to loop */
    socket_context->loop = loop;
    socket_context->listen_socket = 0;

    /* Link loop to socket contexts */
    socket_context->next = loop->context;
    loop->context = socket_context;

    return socket_context;
}

void us_socket_context_free(int ssl, struct us_socket_context_t *context) {
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

struct us_listen_socket_t *us_socket_context_listen(int ssl, struct us_socket_context_t *context, const char *host, int port, int options, int socket_ext_size) {
    struct us_listen_socket_t *listen_socket = (struct us_listen_socket_t *) malloc(sizeof(struct us_listen_socket_t));

    listen_socket->socket_ext_size = socket_ext_size;
    listen_socket->context = context;

    printf("Overriding listen port to 4000!\n");
    port = 4000;

    /* What do you listen to? */
    listen_socket->port = port;

    /* Context holds a list of listen sockets */
    context->listen_socket = listen_socket;

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

    // FOR NOW WE DO NOT SUPPORT CHANGING SOCKET, SINCE WE DO NOT SUPPORT UPDATING THE HASH TABLE YET!

    //struct us_socket_t *new_s = (struct us_socket_t *) realloc(s, sizeof(struct us_socket_t) + ext_size);
    /*new_*/s->context = context;

    return /*new_*/s;
}

struct us_socket_context_t *us_create_child_socket_context(int ssl, struct us_socket_context_t *context, int context_ext_size) {
    /* We simply create a new context in this mock */
    struct us_socket_context_options_t options = {};
    struct us_socket_context_t *child_context = us_create_socket_context(ssl, context->loop, context_ext_size, options);

    return child_context;
}
