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

int main (void)
{
    IP ip; //swappable IP driver - add test IP driver!
    Tcp t(&ip, 4000); //t.listen

    t.onConnection([](Socket *socket) {
        std::cout << "[Connection] Connetions: " << ++connections << std::endl;

        // called for both client and server sockets
        socket->send("Hello over there!", 17);


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

    t.connect("127.0.0.1:4000", nullptr);

    t.run();
}
