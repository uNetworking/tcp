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

#include <chrono>
#include <fstream>

std::chrono::system_clock::time_point then = std::chrono::system_clock::now();
int sampleBytes = 0, totalBytes = 0;

std::ofstream fout("/home/alexhultman/tcpDownload.zip");

void testDownload() {

    IP ip;
    Context t(&ip, 4000);

    // Context.getIP().setSourceAddress();
    // Context.getIP().setSourceHardwareAddress();
    // Context.getIP().setDestinationHardwareAddress();

    // Context.listen(port);
    // Context.connect();

    auto dataHandler = [](Socket *socket, char *data, size_t length) {

        static bool first = true;

        // strip HTTP response header at first
        if (first) {
            char *strippedData = strstr(data, "\r\n\r\n") + 4;
            length -= (strippedData - data);
            data = strippedData;
            first = false;
        }

        // save data to disk, compare checksum later on
        fout.write(data, length);

        // display download speed
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - then).count() >= 500) {

            sampleBytes *= 2;

            std::cout << (sampleBytes / 1024) << " kb/sec, " << (float(totalBytes) / (1024 * 1024 * 1024)) * 100 << "%" << std::endl;
            sampleBytes = 0;
            then = now;
        }
        totalBytes += length;
        sampleBytes += length;

        if (totalBytes == 1024 * 1024 * 1024) {
            std::cout << "Downloaded in full" << std::endl;
        }
    };

    auto disconnectionHandler = [](Socket *socket) {
        std::cout << "Disconnected" << std::endl;
        std::cout << (float(totalBytes) / (1024 * 1024 * 1024)) * 100 << "%" << std::endl;
        fout.close();
    };

    t.addState(0, {dataHandler, disconnectionHandler});

    // Context.setSocketAllocator()
    // Context.replaceSocketMemory(

    t.onConnection([](Socket *socket) {

        socket->setState(0);

        std::cout << "Sending HTTP GET" << std::endl;
        socket->send(httpGet, sizeof(httpGet) - 1);
        totalBytes = 0;
    });

    // this will create a Socket right now, set state here?
    t.connect("192.168.1.104:4001", "80.249.99.148:80", nullptr);
    t.run();
}

//void testEcho() {
//    IP ip; //swappable IP driver - add test IP driver!
//    Context t(&ip, 4000); //t.listen

//    t.onConnection([](Socket *socket) {
//        std::cout << "[Connection] Connetions: " << ++connections << std::endl;

//        // called for both client and server sockets
//        if (connections == 1) {
//            socket->send("Hello over there!", 17);
//            socket->send("Hello over there?", 17);
//        }

//        // send tons of data here (both directions)
//    });

//    t.onDisconnection([](Socket *socket) {
//        std::cout << "[Disconnection] Connetions: " << --connections << std::endl;
//    });

//    // socket->haltReceive
//    t.onData([](Socket *socket, char *data, size_t length) {
//        std::cout << "Received data: " << std::string(data, length) << std::endl;

//        if (received++ < 2) {
//            //socket->send("Thanks", 6);
//        }

//        /*socket->send("------\n", 7);
//        socket->send(data, length);
//        socket->send("------\n", 7);*/
//    });

//    // t.listen() -> vector of ports in use

//    t.connect("127.0.0.1:4001", "127.0.0.1:4000", nullptr);

//    t.run();
//}

//void testReceive() {
//    IP ip;
//    Context t(&ip, 4000);

//    t.onConnection([](Socket *socket) {
//        std::cout << "Connected" << std::endl;
//    });

//    t.onDisconnection([](Socket *socket) {
//        std::cout << "Disconnected" << std::endl;
//    });

//    t.onData([](Socket *socket, char *data, size_t length) {
//        std::cout << "Now at " << (float(received) / (1024 * 1024 * 1024)) * 100 << "%" << std::endl;
//        received += length;
//    });

//    t.run();
//}

int main (void) {
    //testReceive(); // use netcat locally to transfer data
    //testEcho();
    testDownload();
}
