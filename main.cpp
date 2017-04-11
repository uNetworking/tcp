#include "Tcp.h"

int connections = 0;

// Mimic Node & Socket of µWS
// Maybe C-only?
// Transfer test on localhost and via lossy IP driver that reorders
// and drops random packets for robust testing under stress
// Minimal interface, minimal feature set, simple
// Implement in both user space and via epoll as fallback in one module?
// Separate TCP layer from µWS into this?

int received = 0;

char httpGet[] = "GET /1GB.zip HTTP/1.1\r\nHost: 80.249.99.148\r\n\r\n";

void testDownload() {
    IP ip;
    Tcp t(&ip, 4000);

    t.onConnection([](Socket *socket) {
        std::cout << "Sending HTTP GET" << std::endl;
        socket->send(httpGet, sizeof(httpGet) - 1);
    });

    t.onDisconnection([](Socket *socket) {
        std::cout << "Disconnected" << std::endl;
    });

    t.onData([](Socket *socket, char *data, size_t length) {
        //std::cout << "Received " << length << " bytes of data" << std::endl;
        std::cout << "Now at " << (float(received) / (1024 * 1024 * 1024)) * 100 << "%" << std::endl;
        received += length;
    });

    t.connect("192.168.1.104:4001", "80.249.99.148:80", nullptr);
    t.run();
}

void testEcho() {
    IP ip; //swappable IP driver - add test IP driver!
    Tcp t(&ip, 4000); //t.listen

    t.onConnection([](Socket *socket) {
        std::cout << "[Connection] Connetions: " << ++connections << std::endl;

        // called for both client and server sockets
        if (connections == 1) {
            socket->send("Hello over there!", 17);
            socket->send("Hello over there?", 17);
        }

        // send tons of data here (both directions)
    });

    t.onDisconnection([](Socket *socket) {
        std::cout << "[Disconnection] Connetions: " << --connections << std::endl;
    });

    // socket->haltReceive
    t.onData([](Socket *socket, char *data, size_t length) {
        std::cout << "Received data: " << std::string(data, length) << std::endl;

        if (received++ < 2) {
            //socket->send("Thanks", 6);
        }

        /*socket->send("------\n", 7);
        socket->send(data, length);
        socket->send("------\n", 7);*/
    });

    // t.listen() -> vector of ports in use

    t.connect("127.0.0.1:4001", "127.0.0.1:4000", nullptr);

    t.run();
}

int main (void) {
    //testEcho();
    testDownload();
}
