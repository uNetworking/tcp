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
//            sendVec[i].msg_hdr.msg_iov = &messages[packages - i - 1];
//            sendVec[i].msg_hdr.msg_iovlen = 1;

//            sendVec[i].msg_hdr.msg_name = &sin[packages - i - 1];
//            sendVec[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);

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
