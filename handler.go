// Package rdmahandler 提供了用于RDMA（Remote Direct Memory Access）通信的处理程序和接口。
// 它封装了与RDMA相关的操作，允许用户方便地初始化服务器或客户端，进行数据读写，以及销毁资源。
package rdmahandler

/*
#cgo LDFLAGS: -libverbs
#include "rdma_operations.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

type RDMACommunicator interface {
	InitServer(port int) (*RDMAResources, error)
	InitClient(ip string, port int) (*RDMAResources, error)
	Write(res *RDMAResources, contents string, character string) error
	Read(res *RDMAResources, character string) (string, error)
	Destroy(res *RDMAResources) error
}

// RDMAHandler 实现了 RDMACommunicator 接口，提供了具体的 RDMA 通信功能。
// 它包含了为 RDMA 通信所需的所有操作，包括服务器和客户端的初始化、
// 数据读写，以及资源的释放。
//
// 作为 RDMACommunicator 的具体实现，RDMAHandler 提供了这些方法的具体逻辑，
// 使得可以在 RDMA 网络环境中高效地进行数据传输和管理。
//
// Example of usage:
//
//	handler := rdmahandler.RDMAHandler{}
//	res, err := handler.InitServer(8080)
//	if err != nil {
//	    log.Fatalf("Server initialization failed: %v", err)
//	}
//	// Use handler to perform RDMA operations
//	...
type RDMAHandler struct{}

// InitServer initializes an RDMA server on the specified port. It sets up
// the necessary RDMA resources and returns a pointer to these resources along with
// any error encountered during the setup.
//
// `port` is the port number on which the RDMA server will listen. It should be a valid
// port number where the server has permissions to bind.
//
// On success, it returns a pointer to the initialized RDMAResources and nil error.
// On failure, it returns nil and the error encountered.
//
// Example:
//
//	res, err := h.InitServer(8080)
//	if err != nil {
//	    log.Fatalf("Failed to initialize RDMA server: %v", err)
//	}
//	// Use res (RDMAResources) as needed
//	...
func (h *RDMAHandler) InitServer(port int) (*RDMAResources, error) {
	return initRDMAConnection("", port)
}

// InitClient establishes a connection to an RDMA server at the specified IP address and port.
// It initializes the client-side RDMA resources and returns a pointer to these resources, along with
// any error encountered during the connection and initialization process.
//
// `ip` is the IP address of the RDMA server to connect to. It should be a valid IPv4 or IPv6 address.
// `port` is the port number on which the RDMA server is listening. It should be a valid port number
// where the server is expecting connections.
//
// On success, it returns a pointer to the initialized RDMAResources and nil error. On failure, it
// returns nil and the error encountered.
//
// Example:
//
//	clientRes, err := h.InitClient("192.168.1.10", 8080)
//	if err != nil {
//	    log.Fatalf("Failed to initialize RDMA client: %v", err)
//	}
//	// Use clientRes (RDMAResources) for client-side operations
//	...
func (h *RDMAHandler) InitClient(ip string, port int) (*RDMAResources, error) {
	return initRDMAConnection(ip, port)
}

//	Write sends the given contents to a remote RDMA peer using the specified RDMAResources.
//
// It performs an RDMA write operation and ensures the data synchronization.
//
// `res` is a pointer to RDMAResources which should be previously initialized and represent
// an established RDMA connection.
//
// `contents` is the string data to be sent to the remote peer.
//
// `character` is used in error messages to identify the operation or the role of the peer
// (e.g., "client" or "server").
//
// This function first synchronizes the data, then performs the RDMA write operation, and
// finally checks for completion. Any error encountered during these steps is returned.
//
// On success, it returns nil. On failure, it returns an error detailing the issue encountered.
//
// Example:
//
//	err := h.Write(clientRes, "Hello RDMA", "client")
//	if err != nil {
//	    log.Fatalf("RDMA write failed: %v", err)
//	}
func (h *RDMAHandler) Write(res *RDMAResources, contents string, character string) error {
	if err := syncData(res); err != nil {
		return err
	}
	cContents := C.CString(contents)
	defer C.free(unsafe.Pointer(cContents))

	C.strcpy(res.res.buf, cContents)

	if C.post_send(&res.res, C.IBV_WR_RDMA_WRITE) != 0 {
		return fmt.Errorf("%s: failed to post SR", character)
	}
	if C.poll_completion(&res.res) != 0 {
		return fmt.Errorf("%s: poll completion failed", character)
	}
	if err := syncData(res); err != nil {
		return err
	}
	return nil
}

// Read performs an RDMA read operation using the given RDMAResources and retrieves data from a remote RDMA peer.
//
// `res` is a pointer to RDMAResources that must be previously initialized and represent
// an established RDMA connection.
//
// `character` is a string used to identify the operation or the role of the peer in error messages
// (e.g., "client" or "server").
//
// This function synchronizes the data before and after the RDMA read operation. If any error
// occurs during these steps, the function returns an empty string along with the error.
//
// On successful completion of the read operation, it returns the read data as a string and nil error.
// On failure, it returns an empty string and the error encountered.
//
// Example:
//
//	data, err := h.Read(serverRes, "server")
//	if err != nil {
//	    log.Fatalf("RDMA read failed: %v", err)
//	}
//	fmt.Println("Received data:", data)
func (h *RDMAHandler) Read(res *RDMAResources, character string) (string, error) {
	if err := syncData(res); err != nil {
		return "", err
	}
	if C.post_send(&res.res, C.IBV_WR_RDMA_READ) != 0 {
		return "", fmt.Errorf("%s: failed to post SR", character)
	}
	if C.poll_completion(&res.res) != 0 {
		return "", fmt.Errorf("%s: poll completion after post_send failed", character)
	}
	if err := syncData(res); err != nil {
		return "", err
	}
	return C.GoString(res.res.buf), nil
}

// Destroy releases the resources allocated for an RDMA connection.
//
// `res` is a pointer to RDMAResources that should be previously allocated and used
// for an RDMA connection. This function is responsible for properly releasing these
// resources to avoid resource leaks.
//
// It attempts to destroy the RDMA resources by calling the appropriate C function.
// If the resources cannot be successfully destroyed, the function returns an error
// detailing the failure.
//
// On success, it returns nil, indicating the resources were successfully released.
// On failure, it returns an error.
//
// Example:
//
//	err := h.Destroy(clientRes)
//	if err != nil {
//	    log.Fatalf("Failed to destroy RDMA resources: %v", err)
//	}
func (h *RDMAHandler) Destroy(res *RDMAResources) error {
	if C.resources_destroy(&res.res) != 0 {

		return fmt.Errorf("failed to destroy resources")
	}
	return nil
}

// RDMAResources encapsulates the resources required for establishing and managing
// an RDMA (Remote Direct Memory Access) connection. It serves as a wrapper around
// the C-level struct_resources, providing a Go-friendly interface for RDMA operations.
//
// The `res` field is an instance of C.struct_resources, which holds the necessary
// RDMA resources and configurations such as the protection domain, memory regions,
// queue pairs, and other essential components for establishing RDMA connections.
//
// This struct is used throughout the RDMA handling code to maintain the state and
// resources of an RDMA connection, either as a client or a server.
//
// Example of usage:
//
//	var resources RDMAResources
//	// Initialize RDMA resources for a client or server
//	// Use resources in RDMA operations such as Read, Write, etc.
//	...
type RDMAResources struct {
	res C.struct_resources
}

// initRDMAConnection initializes the RDMA resources and establishes a connection
// either as a client or a server based on the provided IP address.
//
// `ip` is the IP address of the RDMA server to connect to. If `ip` is an empty string,
// the function sets up as a server, otherwise it sets up as a client.
//
// `port` is the port number used for the RDMA connection.
//
// This function configures the RDMA connection parameters, creates the necessary
// resources, and connects the queue pairs (QPs). If any step in this process fails,
// it cleans up any partially created resources and returns an error.
//
// On success, it returns a pointer to the initialized RDMAResources and nil error.
// On failure, it returns nil and an error explaining the failure.
//
// Example:
//
//	res, err := initRDMAConnection("192.168.1.10", 8080)
//	if err != nil {
//	    log.Fatalf("RDMA connection initialization failed: %v", err)
//	}
func initRDMAConnection(ip string, port int) (*RDMAResources, error) {
	var resources RDMAResources

	serverAddr := C.CString(ip)
	defer C.free(unsafe.Pointer(serverAddr))

	if ip != "" {
		fmt.Println("client now setting up")
		C.config.server_name = serverAddr
	} else {
		fmt.Println("server now setting up")
		C.config.server_name = nil
	}
	C.config.tcp_port = C.uint32_t(port)

	if C.resources_create(&resources.res) != 0 {
		return nil, fmt.Errorf("failed to create resources")
	}
	if C.connect_qp(&resources.res) != 0 {
		C.resources_destroy(&resources.res)
		return nil, fmt.Errorf("failed to connect QPs")
	}
	return &resources, nil
}

// syncData synchronizes data over the socket associated with the provided RDMA resources.
//
// `res` is a pointer to RDMAResources which should be previously initialized and represent
// an established RDMA connection.
//
// This function attempts to synchronize data across the connection by sending a single
// character ('R') and expecting to receive a character back. This ensures both sides of
// the RDMA connection are ready to proceed with further operations.
//
// If the synchronization fails, the function returns an error detailing the issue.
//
// On success, it returns nil, indicating successful synchronization.
// On failure, it returns an error.
//
// Example:
//
//	err := syncData(serverRes)
//	if err != nil {
//	    log.Fatalf("Data synchronization failed: %v", err)
//	}
func syncData(res *RDMAResources) error {
	var tempChar C.char
	if C.sock_sync_data(res.res.sock, 1, C.CString("R"), &tempChar) != 0 {
		return fmt.Errorf("sync error")
	}
	return nil
}
