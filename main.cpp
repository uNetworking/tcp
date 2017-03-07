#include "Tcp.h"

int connections = 0;

int main (void)
{
    Tcp t;

    t.onConnection([](Socket *socket) {
        std::cout << "[Connection] Connetions: " << ++connections << std::endl;
    });

    t.onDisconnection([](Socket *socket) {
        std::cout << "[Disconnection] Connetions: " << --connections << std::endl;
    });

    t.onData([](char *data, size_t length) {
        std::cout << "Received data: " << std::string(data, length) << std::endl;
    });

    t.run();
}
