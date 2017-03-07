# raw
#### Proof-of-concept TCP implementation with no per-socket buffers.

This is an experimentation project with the goal of establishing TCP connections with no buffers. Instead of implementing the BSD/epoll interfaces (which require big per-socket buffers) this experimentation implementation makes use of immediate callbacks to pass data to application level. This allows (according to hypothesis) very lightweight connections, only a few bytes each.

Currently one "connection" (VERY limited) requires about 14 bytes of memory (no kernel space memory needed). You can thus establish millions of connections using virtually no memory at all.

As a comparison with BSD sockets, instead of requiring ~4gb of kernel space memory per 1 million connections, you can get by with less than 20mb of user space memory and 0 bytes of kernel space memory.

```
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

```
