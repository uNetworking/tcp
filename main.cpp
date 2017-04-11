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

// drop all TCP input on port 4000
// sudo iptables -A INPUT -p tcp --destination-port 4000 -j DROP

int packets = 0;

char httpGet[] = "GET /1GB.zip HTTP/1.1\r\nHost: 80.249.99.148\r\n\r\n";

int main (void)
{
    IP ip; //swappable IP driver - add test IP driver!
    Tcp t(&ip, 4000); //t.listen

    // send(message, callback)
    // och diverse liknande!
    // callback är när den har blivit sänd, inte emottagen!
    // samma som BSD, men meddelandet hålls kvar längre (allokering för varje send)

    // callbacken har olika betydelse men är "ge mig mer data nu"

    t.onConnection([](Socket *socket) {
        std::cout << "[Connection] Connetions: " << ++connections << std::endl;

        // send simple HTTP GET request
        socket->send(httpGet, sizeof(httpGet) - 1);
    });

    t.onDisconnection([](Socket *socket) {
        std::cout << "[Disconnection] Connetions: " << --connections << std::endl;
    });

    // socket->haltReceive
    t.onData([](Socket *socket, char *data, size_t length) {
        std::cout << clock() << " Received length: " << length << std::endl;
    });

    // t.listen() -> vector of ports in use

    // connect to google
    //t.connect("192.168.1.71", "172.217.23.35:4000", nullptr);

    // download 1gb.zip
    t.connect("192.168.1.71", "80.249.99.148", nullptr);

    t.run();
}
