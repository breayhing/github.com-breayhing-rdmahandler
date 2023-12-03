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

// RDMACommunicator 接口定义了RDMA通信所需的基本方法
type RDMACommunicator interface {
	InitServer(port int) (*RDMAResources, error)
	InitClient(ip string, port int) (*RDMAResources, error)
	Write(res *RDMAResources, contents string, character string) error
	Read(res *RDMAResources, character string) (string, error)
	Destroy(res *RDMAResources) error
}

// RDMAHandler 实现了RDMACommunicator接口
type RDMAHandler struct{}

// InitServer 初始化RDMA服务器
func (h *RDMAHandler) InitServer(port int) (*RDMAResources, error) {
	return initRDMAConnection("", port)
}

// InitClient 初始化RDMA客户端
func (h *RDMAHandler) InitClient(ip string, port int) (*RDMAResources, error) {
	return initRDMAConnection(ip, port)
}

// Write 向指定的角色写入数据
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

// Read 从指定的角色读取数据
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

// Destroy 销毁RDMA资源
func (h *RDMAHandler) Destroy(res *RDMAResources) error {
	if C.resources_destroy(&res.res) != 0 {

		return fmt.Errorf("failed to destroy resources")
	}
	return nil
}

// RDMAResources 存储RDMA相关的资源
type RDMAResources struct {
	res C.struct_resources
}

// initRDMAConnection 用于初始化RDMA连接
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

// syncData 用于数据同步
func syncData(res *RDMAResources) error {
	var tempChar C.char
	if C.sock_sync_data(res.res.sock, 1, C.CString("R"), &tempChar) != 0 {
		return fmt.Errorf("sync error")
	}
	return nil
}
