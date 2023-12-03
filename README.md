# rdmahandler

`rdmahandler` 是一个 Go 语言包，专门用于实现 RDMA（Remote Direct Memory Access）通信。此包提供了一系列工具和接口，使用户能够方便地初始化服务器或客户端，进行数据读写，以及管理和销毁 RDMA 相关资源。

## 功能

- **初始化 RDMA 服务器和客户端**：通过 `InitServer` 和 `InitClient` 方法，用户可以轻松地设立 RDMA 服务器或作为客户端连接到 RDMA 服务器。
- **数据读写**：`Write` 和 `Read` 方法允许在 RDMA 连接上进行高效的数据传输。
- **资源管理**：`Destroy` 方法用于正确释放 RDMA 连接所使用的资源，确保资源的妥善管理。

## 接口和类型

- `RDMACommunicator`：此接口定义了用于 RDMA 通信的基本方法集。
- `RDMAHandler`：实现了 `RDMACommunicator` 接口，提供具体的 RDMA 通信功能。
- `RDMAResources`：封装了建立和管理 RDMA 连接所需的资源。

## 示例使用

```go
handler := rdmahandler.RDMAHandler{}
res, err := handler.InitServer(8080)
if err != nil {
    log.Fatalf("Server initialization failed: %v", err)
}
// 使用 handler 执行 RDMA 操作
...

```

## 安装

使用 `go get` 命令来安装 rdmahandler:

```bash
go get github.com/breayhing/rdmahandler
```
