# rdmahandler

`rdmahandler` is a Go language package specifically for implementing RDMA (Remote Direct Memory Access) communication. This package provides a series of tools and interfaces that enable users to easily initialize servers or clients, read and write data, and manage and destroy RDMA-related resources.

## Function

- **Initialize RDMA server and client**: Through `InitServer` and `InitClient` methods, users can easily set up an RDMA server or connect to an RDMA server as a client.
- **Data reading and writing**: `Write` and `Read` methods allow efficient data transfer on RDMA connections.
- **Resource management**: `Destroy` method is used to properly release resources used by RDMA connections and ensure proper resource management.

## Interfaces and Types

- `RDMACommunicator`: This interface defines the basic set of methods for RDMA communication.
- `RDMAHandler`: Implements the `RDMACommunicator` interface and provides specific RDMA communication functions.
- `RDMAResources`: Encapsulates the resources required to establish and manage RDMA connections.

## Example Usage

```go
handler := rdmahandler.RDMAHandler{}
res, err := handler.InitServer(8080)
if err != nil {
    log.Fatalf("Server initialization failed: %v", err)
}
// Use handler to perform RDMA operations
...

```

## Install

Use the `go get` command to install rdmahandler:

```bash
go get github.com/liver/rdmahandler
```
