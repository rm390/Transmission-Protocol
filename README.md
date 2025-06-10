# Transmission-Protocol

This project implements a connection-oriented, reliable transport protocol on top of UDP, using a Adaptive-RQ retransmission mechanism. It supports reliable, in-order, bi-directional data delivery between a single client and server.

## Compilation

Using a linux system build the project.

```sh
make
```

This will compile the server and client binaries.

## Running 

To run tests two machines on the same network are required, one to be the server and one to be the client.

```sh
# Terminal 1 (server)
./test-client-{0,1,2,3}

# Terminal 2 (client)
./test-server-{0,1,2,3} <server-hostname>
```
