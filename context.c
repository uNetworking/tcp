// socket context implements tcp protcol

/* uSockets is entierly opaque so we can use the real header straight up */
#include "../uWebSockets.js/uWebSockets/uSockets/src/libusockets.h"

#include "internal.h"

/* Static timer info */
struct us_internal_socket_timer_t {
    struct us_socket_t *s;
    int ticks;
    /* For now we have a callback */
    void (*cb)(struct us_socket_t *);
};

// per loop, per application
struct us_internal_socket_timer_t *timers = 0;
int num_timers = 1000000;

void us_internal_socket_timeout(struct us_internal_socket_timer_t *timer);


/* Timer logic could be separate module */
void us_internal_small_tick() {

    /* Init timers */
    if (timers == 0) {
        timers = calloc(1000000, sizeof(struct us_internal_socket_timer_t));
    }

    /* Sweep them up to num_timers */
    for (int i = 0; i < num_timers; i++) {
        /* Is this a gap? */
        if (timers[i].s && timers[i].ticks) {
            if (--timers[i].ticks == 0) {
                /* Timer has triggered (this may rearm the timer but only itself no other socket?) */
                us_internal_socket_timeout(&timers[i]);
            }
        }
    }

}

/* Arms or resets a timer based on increments of 250ms.
   Settings to 0 disables the timer.  */
void us_internal_add_timeout(int ticks, struct us_socket_t *s) {

    /* Sweep from start to end until a gap is there */
    for (int i = 0; i < 1000000; i++) {
        /* This slot is expired */
        if (!timers[i].ticks) {
            timers[i].ticks = ticks;
            timers[i].s = s;
            break;
        }
    }
}

void us_internal_remove_timeout(struct us_socket_t *s) {
    for (int i = 0; i < 1000000; i++) {
        /* This slot is expired */
        if (timers[i].s == s) {
            timers[i].ticks = 0;
            timers[i].s = 0;
            break;
        }
    }
}

// socket container with lookup

#define SOCKET_SYN_ACK_SENT 1
#define SOCKET_ESTABLISHED 2
#define SOCKET_SYN_SENT 3

/* Socket timeout handler cannot call us_internal_set_timeot but can access the timer directly */
void us_internal_socket_timeout(struct us_internal_socket_timer_t *timer) {
    //printf("Socket timed out!\n");

    struct us_socket_t *s = timer->s;

    if (timer->s->state == SOCKET_SYN_ACK_SENT) {
        printf("RETRANSMITTING SYN,ACK!\n");

        if (s->initialHostSeq != s->hostSeq) {
            printf("RETRANSMITTING WRONG? %d vs. %d\n", s->initialHostSeq, s->hostSeq);
        }

        if (s->initialRemoteSeq != s->hostAck) {
            printf("RETRANSMITTING WRONG (ack)? %d vs. %d\n", s->initialRemoteSeq, s->hostAck);
        }

        /* Send syn, ack */
        us_internal_socket_context_send_packet(s->context, s->hostSeq, s->hostAck + 1, s->networkIp, s->networkDestinationIp, s->hostPort, s->hostDestinationPort, 1, 1, 0, 0, NULL, 0);

        /* Remove this timer */
        //timer->s = 0;

        /* Rearm this timer */
        timer->ticks = 2;
    }
}

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

    if (rand() % 10 == 1) {
        printf("Dropping packet now!\n");
        return;
    }

    // for testing: send RST when sending FIN
    // the iptables rule filters out the packet when RST flag is set!
    if (flagFin) {
        //flagRst = 1;
    }


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

    printf("%fs [Outgoing ", 100.0f * (float) clock() / (float) CLOCKS_PER_SEC);
    print_packet(tcpHeader);
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

    global_s->prev = 0;
    global_s->next = 0;

    return global_s;
}

void remove_socket(struct us_socket_t *s);

//us_socket_write can take identifier to merge common sends into one

print_packet(struct TcpHeader *tcpHeader) {
    printf("packet, seq %u, ack_seq: %u", ntohl(tcpHeader->header.seq), ntohl(tcpHeader->header.ack_seq));
    /* Får vi någonsin RST? */
    if (tcpHeader->header.rst) {
        printf(", RST");
    }
    if (tcpHeader->header.syn) {
        printf(", SYN");
    }
    if (tcpHeader->header.ack) {
        printf(", ACK");
    }
    if (tcpHeader->header.fin) {
        printf(", FIN");
    }
    /*if (tcpHeader->header.rst) {
        printf("RST, ");
    }*/
    printf("]\n");
}


// pass tcp data to the context - call it read_packet, send_packet
void us_internal_socket_context_read_tcp(struct us_socket_t *s, struct us_socket_context_t *context, IpHeader *ipHeader, struct TcpHeader *tcpHeader, int length) {

    /* Always debug the incoming packet in terms of flags and syn, syn_ack */
    printf("%fs [Incoming ", 100.0f * (float) clock() / (float) CLOCKS_PER_SEC);
    print_packet(tcpHeader);

    /* Drop packets 10% */
    if (rand() % 10 == 1) {
        printf("Dropping packet now!\n");
        return;
    }

    if (!s) {
        /* Is this a SYN but not an ACK? */
        if (tcpHeader->header.syn && !tcpHeader->header.ack) {
                /* Allocate the socket */
                uint32_t hostAck = ntohl(tcpHeader->header.seq);
                uint32_t hostSeq = rand();

                struct us_socket_t *s = add_socket();

                // store initial numbers for debugging when shit happens
                s->initialRemoteSeq = hostAck;
                s->initialHostSeq = hostSeq;

                s->state = SOCKET_SYN_ACK_SENT;
                s->context = context;
                s->hostAck = hostAck;
                s->hostSeq = hostSeq;

                /* If we are about to overflow */
                if (s->hostAck > UINT32_MAX - 100) {
                    printf("WE ARE GOING TO FLIP SOON!\n");
                    exit(0);
                }

                s->networkIp = ipHeader->saddr;
                s->hostPort = TcpHeader_getSourcePort(tcpHeader);

                s->networkDestinationIp = ipHeader->daddr;
                s->hostDestinationPort = TcpHeader_getDestinationPort(tcpHeader);

                /* Debug */
                s->packets = 1; // obviously we got SYN
                s->mostOutOfSync = 0;

                // what was the initial hostAck of this socket? how many times did we retransmit? what is it now?
                // did we retransmit wrong?

                /* Test: We do not respond here, but in the timeout instead (we simulate dropping the SynAck) */

                /* Send syn, ack */
                us_internal_socket_context_send_packet(s->context, s->hostSeq, s->hostAck + 1, s->networkIp, s->networkDestinationIp, s->hostPort, s->hostDestinationPort, 1, 1, 0, 0, NULL, 0);

                /* We require an ACK within 500 ms, or we retransmit */
                us_internal_add_timeout(2, s);

                /* Now we will return, and global_s is added to the hash table in loop */
        } else {
            /* All other packtes in this state are uninvited */
            printf("Dropping uninvited packet\n");
        }
    } else {

        // what if we get an extra SYN here?
        if (tcpHeader->header.syn) {
            printf("GOT DUPLICATE SYN!\n");
            return;
        }

        // öka paket för statistik
        context->loop->packets_received++;

        // SYN allokerar socketen, alla nästkommande paket räknas upp
        s->packets++;

        /*if (s->state == SOCKET_SYN_ACK_SENT) {
            // if we get a SYN resent in this state, we know we lost it
            if (tcpHeader->header.syn && !tcpHeader->header.ack) {
                printf("WE WERE RESENT A SYN WHILE ALREADY IN SYN-ACK-SENT STATE!\n");
                //us_internal_socket_context_read_tcp()
                return;
            }
        }*/


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

                // if our syn-ack is lost, we believe we sent it but we haven't

                /* Here we are established, and we may also get data */
                uint32_t seq = ntohl(tcpHeader->header.seq);
                uint32_t ack = ntohl(tcpHeader->header.ack_seq);

                if (s->hostAck + 1 == seq && s->hostSeq + 1 == ack) {
                    s->hostAck++;
                    s->hostSeq++;
                    s->state = SOCKET_ESTABLISHED;

                    /* We are now established, remove timeout for socket */
                    us_internal_remove_timeout(s);

                    /* link it with this context for sweeps */
                    us_internal_socket_context_link(context, s);

                    /* Emit open event */
                    context->on_open(s, 0, "nej!", 0);

                    static int sockets;
                    sockets++;

                    if (sockets % 1000 == 0)
                    printf("Sockets open: %d\n", sockets);

                } else {

                    // vi får fel ack från clienten, förmodligen skickar vi fel seq i retransmit - jämför mot initial?

                    // dessa stämmer
                    printf("Expected: %d got: %d\n", s->hostAck + 1, seq);

                    // dessa stämmer inte?
                    printf("Expected: %d got: %d or got: %d\n", s->hostSeq + 1, ack, ntohl(ack));

                    printf("Initial numbers: %d, %d\n", s->initialHostSeq, s->initialRemoteSeq);

                    // vi kommer hit utan data nu? skickar vi till oss själva?

                    if (tcpHeader->header.syn) {

                        printf("VI TOG EMOT ETT SYN,ACK!???\n");
                    }

                    if (tcpHeader->header.rst) {

                        printf("Tog vi emot ett RST?\n");
                    }

                    // what if we lost the SYN -> SYN_ACK -> [ACK] from the client, followed by next packet which is ACK with data?

                    int tcpdatalen = ntohs(ipHeader->tot_len) - (tcpHeader->header.doff * 4) - (ipHeader->ihl * 4);

                    printf("Server ack is wrong, and we got data? of length: %d\n", tcpdatalen);
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

            /* Simple check: if the difference between the two is very large, then overflow error? */
            //if ()


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

                /* If we are about to overflow */
                if (s->hostAck > UINT32_MAX - 100) {
                    printf("WE ARE GOING TO FLIP SOON!\n");
                    exit(0);
                }

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

void us_internal_socket_context_link(struct us_socket_context_t *context, struct us_socket_t *s) {
    s->context = context;
    s->timeout = 0;
    s->next = context->head;
    s->prev = 0;
    if (context->head) {
        context->head->prev = s;
    }
    context->head = s;
}

struct us_socket_context_t *us_create_socket_context(int ssl, struct us_loop_t *loop, int ext_size, struct us_socket_context_options_t options) {
    struct us_socket_context_t *socket_context = (struct us_socket_context_t *) malloc(sizeof(struct us_socket_context_t) + ext_size);

    /* Link socket context to loop */
    socket_context->loop = loop;
    socket_context->listen_socket = 0;

    /* Link loop to socket contexts */
    socket_context->next = loop->context;
    loop->context = socket_context;

    socket_context->head = 0;
    socket_context->iterator = 0;
    socket_context->prev = 0;

    us_internal_loop_link(loop, socket_context);

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
    context->on_socket_timeout = on_timeout;
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

struct NetworkAddress {
    uint32_t addr;
    uint16_t port;
};

// networkAddress, hostPort
struct NetworkAddress networkAddressFromString(char *address) {
    unsigned int addr[5];
    sscanf(address, "%d.%d.%d.%d:%d", &addr[0], &addr[1], &addr[2], &addr[3], &addr[4]);

    uint32_t networkAddress = addr[0] << 24 | addr[1] << 16 | addr[2] << 8 | addr[3];
    struct NetworkAddress na = {htonl(networkAddress), addr[4]};

    return na;
}

/*void Context::connect(char *source, char *destination, void *userData)
{
    // these should return an Endpoint straight up
    struct NetworkAddress sourceAddress = networkAddressFromString(source);
    struct NetworkAddress destinationAddress = networkAddressFromString(destination);

    Endpoint endpoint = {destinationAddress.first, destinationAddress.second, sourceAddress.first, sourceAddress.second};

    uint32_t hostSeq = rand();
    sockets[endpoint] = new Socket({nullptr, destinationAddress.first, destinationAddress.second, sourceAddress.first, sourceAddress.second, 0, hostSeq, Socket::SYN_SENT});

    Socket::sendPacket(hostSeq, 0, destinationAddress.first, sourceAddress.first, destinationAddress.second, sourceAddress.second, false, true, false, false, nullptr, 0);
    ip->releasePackageBatch();
}*/

// connect sends a syn but ends up in similar path as the rest pretty quickly
struct us_socket_t *us_socket_context_connect(int ssl, struct us_socket_context_t *context, const char *host, int port, const char *interface, int options, int socket_ext_size) {
    printf("us_socket_context_connect\n");

    // these should return an Endpoint straight up
    struct NetworkAddress sourceAddress = networkAddressFromString("127.0.0.1:45000");
    struct NetworkAddress destinationAddress = networkAddressFromString("127.0.0.1:4000");

    // we use 127.0.0.1 as our ip always, and we pick ephemeral port from a list

    //uint32_t clientIP = 123123123;
    //uint16_t clientPort = 12330;

    /* Allocate the socket */
    //uint32_t hostAck = ntohl(tcpHeader->header.seq);
    /*uint32_t hostSeq = rand();

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
    //s->packets = 1; // obviously we got SYN
    //s->mostOutOfSync = 0;

    /* Send syn, ack */
    //us_internal_socket_context_send_packet(context, hostSeq, 0, destinationAddress.addr, ipHeader->daddr, ntohs(tcpHeader->header.source), ntohs(tcpHeader->header.dest), 1, 1, 0, 0, NULL, 0);



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
