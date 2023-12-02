package main

/*
#cgo LDFLAGS: -libverbs
#include "rdma_operations.h"
*/
import "C"
import (
	"fmt"
	"os"
)

// 定义全局变量来存储命令行参数
var (
	tcpPort int
	ibDev   string
	ibPort  int
	gidIdx  int
	server  string
)

// Helper function to convert Go strings to C strings
func goStrToCStr(goStr string) *C.char {
	return C.CString(goStr)
}

func main() {

	// 处理 serverName
	args := os.Args
	serverName := ""
	fmt.Println(len(args))
	// COMMENT:设置为2为阈值
	if len(args) == 2 {
		serverName = args[1]
		C.config.server_name = goStrToCStr(args[1])
		fmt.Printf("Client: servername=%s\n", serverName)
	} else if len(args) > 2 {
		os.Exit(1)
	}

	if serverName != "" {
		fmt.Printf("Running in client mode. Connecting to server at %s\n", serverName)
	} else {
		fmt.Println("Running in server mode")
	}

	C.print_config()
	//开始初始化的部分
	var res C.struct_resources
	var rc int

	C.resources_init(&res)
	if C.resources_create(&res) != 0 {
		fmt.Fprintf(os.Stderr, "failed to create resources\n")
		return
	}

	// 连接队列对
	if C.connect_qp(&res) != 0 {
		fmt.Fprintf(os.Stderr, "failed to connect QPs\n")
		return
	}

	// 交互循环
	for {
		shouldExit := 0
		var tempChar C.char

		// 服务器逻辑
		if C.config.server_name == nil {
			shouldExit = int(C.receive_message(&res, C.CString("Server")))
			if shouldExit != 0 {
				rc = 0
				break // 替代 goto
			}
			fmt.Printf("Server: Message is: '%s'\n", C.GoString(res.buf))
		}

		// 数据同步
		if C.sock_sync_data(res.sock, 1, C.CString("R"), &tempChar) != 0 {
			fmt.Fprintln(os.Stderr, "sync error before RDMA ops")
			rc = 1
			break
		}

		// 客户端逻辑
		if C.config.server_name != nil {
			// RDMA 读操作
			if C.post_send(&res, C.IBV_WR_RDMA_READ) != 0 {
				fmt.Fprintln(os.Stderr, "Client: failed to post SR 2")
				rc = 1
				break
			}
			if C.poll_completion(&res) != 0 {
				fmt.Fprintln(os.Stderr, "Client: poll completion failed 2")
				rc = 1
				break
			}
			fmt.Printf("Client: Contents of server's buffer: '%s'\n", C.GoString(res.buf))
			shouldExit = int(C.receive_message(&res, C.CString("Client")))
			if shouldExit != 0 {
				rc = 0
				break
			}
			// RDMA 写操作
			if C.post_send(&res, C.IBV_WR_RDMA_WRITE) != 0 {
				fmt.Fprintln(os.Stderr, "Client: failed to post SR 3")
				rc = 1
				break
			}
			if C.poll_completion(&res) != 0 {
				fmt.Fprintln(os.Stderr, "Client: poll completion failed 3")
				rc = 1
				break
			}
		}

		// 同步
		if C.sock_sync_data(res.sock, 1, goStrToCStr("W"), &tempChar) != 0 {
			fmt.Fprintf(os.Stderr, "sync error\n")
			break
		}
		if C.config.server_name == nil {
			fmt.Printf("Server: Contents of client's buffer: '%s'\n", C.GoString(res.buf))
		}

	}

	if C.resources_destroy(&res) != 0 {
		fmt.Fprintf(os.Stderr, "failed to destroy resources\n")
	} else {
		fmt.Println("resources destoryed , now shut down program")
	}
	fmt.Printf("\ntest result is %d\n", rc)
}
