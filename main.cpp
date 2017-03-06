#include "Tcp.h"

int connections = 0;

int main (void)
{
    Tcp t;

    t.onConnection([]() {
        std::cout << "Connetions: " << ++connections << std::endl;
    });

    t.onData([](char *data, size_t length) {
        std::cout << "Received data: " << std::string(data, length) << std::endl;
    });

    t.run();
}
