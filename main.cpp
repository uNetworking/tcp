#include "Tcp.h"

int connections = 0;

int main (void)
{
    IP ip;
    Tcp t(ip, 4000);

    t.onConnection([](Socket *socket) {
        std::cout << "[Connection] Connetions: " << ++connections << std::endl;
    });

    t.onDisconnection([](Socket *socket) {
        std::cout << "[Disconnection] Connetions: " << --connections << std::endl;
    });

    t.onData([](Socket *socket, char *data, size_t length) {
        std::cout << "Received data: " << std::string(data, length) << std::endl;

        socket->send("------\n", 7);
        socket->send(data, length);
        socket->send("------\n", 7);
    });

    t.run();
}
