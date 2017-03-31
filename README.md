## Proof-of-concept TCP implementation with no (fix) per-socket buffers.

This is an experimentation project with the goal of establishing TCP connections with no fix buffers attached. Instead of implementing the BSD/epoll interfaces (which require big per-socket buffers) this experimentation implementation makes use of immediate callbacks to pass data to application level. This allows (according to hypothesis) very lightweight connections, only a few bytes each.

Currently one "connection" (VERY limited) requires about 14 bytes of memory (no kernel space memory needed). You can thus establish millions of connections using virtually no memory at all.

As a comparison with BSD sockets, instead of requiring ~4gb of kernel space memory per 1 million connections, you can get by with less than 20mb of user space memory and 0 bytes of kernel space memory.

Memory usage becomes a product of network problematics, rather than a product of number of connections (memory is only needed in worst case scenarios, like when TCP chunks come out-of-order and need a TCP-reassembly buffer). All buffers are shared to statistically minimze memory usage. If memory is scarce, IP packets are dropped and resent later on (according to spec).

## Design goals
These are initial design goals for the TCP stack targeting minimal memory usage and high throughput

* Positive inspiration: lwIP, uIP
* Negative inspiration: mTCP, BSD sockets, Node.js

#### Sending leaves allocation to application code and exposes ACKs
No allocated send buffers by default, this is up to the application to decide:
* Can use pre-allocated per-socket buffers
* Can use memory pool
* Can use the heap
* Some applications want to know when ACKs arrive

```c++
// application code always does the allocation of packages to send
// should also be possible to use pre-allocated send buffer
Package *package = context->frame(const char *data, size_t length);

// send framed package multiple times if necessary
Socket->send(package);

Context.onPeerReceived([](Package *package) {
  // free to delete package here
  delete package;
});
```


#### Received data is pushed directly to application code
No per-socket receive buffers needed in fast path / best case scenarios like single IP-packet TCP packets, or when data flows in order.

* Will need buffers in worst case scenarios where tcp reassembly has holes
* Could make use of a dynamic memory pool of fixed sized buffers to grow or shrink as needed, when needed (main idea)
* Can be useful to empty NIC reveice queue into per-context buffer though
```c++
Context.onData([](Socket *socket, char *data, size_t length) {
  // application code handles data immediately and returns to the TCP stack when done
  // TCP stack should make sure to empty NIC before calling into application (per-context buffering)
});
```

#### No file descriptors, only pointers
There is no lookup from integer to data structure, but instead an immediate pointer to the data structure is handed out

* Socket struct with member functions
* Context struct is a per-thread resource with no relation to other Contexts or Sockets

#### Swappable drivers
Should be able to run on TUN/TAP, Raw sockets and DPDK

* ip_read: consumes data from the IP layer and emits events
* ip_write: emits data to the IP layer
* Implement IP layer on any driver (DPDK, or whatever)
