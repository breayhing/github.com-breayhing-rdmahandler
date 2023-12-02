package main

/*
#cgo LDFLAGS: -libverbs
#include "rdma_operations.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

// COMMENT:RDMA交流接口所使用的五个函数
type RDMACommunicator interface {
	InitServer(port int) (*RDMAResources, error)
	InitClient(ip string, port int) (*RDMAResources, error)
	Write(contents string, character string) error
	Read(character string) (string, error)
	Destroy(res *RDMAResources) error
}

type RDMAHandler struct{}

func (h *RDMAHandler) InitServer(port int) (*RDMAResources, error) {
	return init_server(port)
}

func (h *RDMAHandler) InitClient(ip string, port int) (*RDMAResources, error) {
	return init_client(ip, port)
}

func (h *RDMAHandler) Write(res *RDMAResources, contents string, character string) error {
	return write(res, contents, character)
}

func (h *RDMAHandler) Read(res *RDMAResources, character string) (string, error) {
	return read(res, character)
}

func (h *RDMAHandler) Destroy(res *RDMAResources) error {
	return destroy(res)
}

// 定义一个结构体，用于存储RDMA相关的资源
type RDMAResources struct {
	res C.struct_resources
}

// Helper function to convert Go strings to C strings
func goStrToCStr(goStr string) *C.char {
	return C.CString(goStr)
}

func destroy(res *RDMAResources) error {
	if C.resources_destroy(&res.res) != 0 {
		return fmt.Errorf("failed to destroy resources")
	}
	// 可以在这里添加其他必要的清理工作，例如释放C字符串等
	return nil
}

// init_server 用于初始化RDMA服务器
func init_server(port int) (*RDMAResources, error) {
	var resources RDMAResources

	C.config.server_name = nil // 服务端不需要设置server_name
	C.config.tcp_port = C.uint32_t(port)

	C.resources_init(&resources.res)
	if C.resources_create(&resources.res) != 0 {
		return nil, fmt.Errorf("Server: failed to create resources")
	}

	if C.connect_qp(&resources.res) != 0 {
		C.resources_destroy(&resources.res)
		return nil, fmt.Errorf("Server: failed to connect QPs")
	}

	return &resources, nil
}

// init_client 用于初始化RDMA客户端
func init_client(ip string, port int) (*RDMAResources, error) {
	var resources RDMAResources

	serverAddr := C.CString(ip)
	defer C.free(unsafe.Pointer(serverAddr))

	C.config.server_name = serverAddr
	C.config.tcp_port = C.uint32_t(port)

	C.resources_init(&resources.res)
	if C.resources_create(&resources.res) != 0 {
		return nil, fmt.Errorf("Client: failed to create resources")
	}

	if C.connect_qp(&resources.res) != 0 {
		C.resources_destroy(&resources.res)
		return nil, fmt.Errorf("Client: failed to connect QPs")
	}

	return &resources, nil
}

// FIXME:等待确认可用
// write 向指定的角色写入数据
func write(res *RDMAResources, contents string, character string) error {
	if err := syncData(res); err != nil {
		return err
	}
	shouldExit := 0
	// 数据同步
	if character == "client" {
		cContents := C.CString(contents)
		defer C.free(unsafe.Pointer(cContents))

		C.strcpy(res.res.buf, cContents)

		if C.post_send(&res.res, C.IBV_WR_RDMA_WRITE) != 0 {
			return fmt.Errorf("client: failed to post SR")
		}
		if C.poll_completion(&res.res) != 0 {
			return fmt.Errorf("client: poll completion failed")
		}
	} else if character == "server" {
		// 服务端逻辑（如果需要的话）
		shouldExit = int(C.receive_message(&res.res, C.CString("Server")))
		if shouldExit != 0 {
			destroy(res)
			return fmt.Errorf("server write failed")
		}
		fmt.Printf("Server: Message is: '%s'\n", C.GoString(res.res.buf))
		// 可能需要将数据写入到特定的内存位置
	}
	return nil
}

// read 从指定的角色读取数据
func read(res *RDMAResources, character string) (string, error) {
	if err := syncData(res); err != nil {
		return "", err
	}
	if character == "client" {
		if C.post_send(&res.res, C.IBV_WR_RDMA_READ) != 0 {
			return "", fmt.Errorf("client: failed to post SR")
		}
		if C.poll_completion(&res.res) != 0 {
			return "", fmt.Errorf("client: poll completion failed")
		}
		return C.GoString(res.res.buf), nil
	} else if character == "server" {
		fmt.Printf("Server: Contents of client's buffer: '%s'\n", C.GoString(res.res.buf))
		return C.GoString(res.res.buf), nil
	}
	return "", fmt.Errorf("invalid character")
}

// syncData 用于数据同步
func syncData(res *RDMAResources) error {
	var tempChar C.char
	if C.sock_sync_data(res.res.sock, 1, C.CString("R"), &tempChar) != 0 {
		return fmt.Errorf("sync error")
	}
	return nil
}
