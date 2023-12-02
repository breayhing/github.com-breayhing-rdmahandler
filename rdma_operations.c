#include <rdma_operations.h>
struct config_t config = {
	NULL,  /* dev_name */
	NULL,  /* server_name */
	19875, /* tcp_port */
	1,	   /* ib_port */
	-1 /* gid_idx */};
/******************************************************************************
Socket operations
For simplicity, the example program uses TCP sockets to exchange control
information. If a TCP/IP stack/connection is not available, connection manager
(CM) may be used to pass this information. Use of CM is beyond the scope of
this example
******************************************************************************/
/******************************************************************************
* Function: sock_connect
*
输入:
servername：要连接的服务器的 URL（在服务器模式下为 NULL）。
port：服务的端口号。

* Output
* none
*
* Returns
* socket (fd) on success, negative error code on failure
*
* Description
否则，在指定端口上监听传入连接。
******************************************************************************/
int sock_connect(const char *servername, int port)
{
	// ：struct addrinfo *resolved_addr 和 *iterator: 用于存储 getaddrinfo 函数返回的地址信息和遍历这些地址的迭代器。
	struct addrinfo *resolved_addr = NULL;
	struct addrinfo *iterator;

	char service[6];

	// int sockfd 和 listenfd: 分别用于存储套接字文件描述符和监听文件描述符。
	int sockfd = -1;
	int listenfd = 0;
	int tmp;

	// ：struct addrinfo hints: 用于指定 getaddrinfo 函数的配置，如套接字类型和协议族。
	struct addrinfo hints =
		{
			// ：.ai_flags = AI_PASSIVE：这个标志表示套接字用于被动监听（例如，用于服务器端口监听），而不是主动连接。
			.ai_flags = AI_PASSIVE,
			// .ai_family = AF_INET：指定地址族为 IPv4。这意味着我们只对 IPv4 地址感兴趣。
			.ai_family = AF_INET,
			//.ai_socktype = SOCK_STREAM：指定套接字类型为流套接字，通常用于 TCP 连接。
			.ai_socktype = SOCK_STREAM};
	if (sprintf(service, "%d", port) < 0)
		goto sock_connect_exit;

	/* Resolve DNS address, use sockfd as temp storage */
	sockfd = getaddrinfo(servername, service, &hints, &resolved_addr);
	if (sockfd < 0)
	{
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(sockfd), servername, port);
		goto sock_connect_exit;
	}

	/* Search through results and find the one we want */
	for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
	{
		sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
		if (sockfd >= 0)
		{
			if (servername)
			{
				/* Client mode. Initiate connection to remote */
				if ((tmp = connect(sockfd, iterator->ai_addr, iterator->ai_addrlen)))
				{
					fprintf(stdout, "failed connect \n");
					close(sockfd);
					sockfd = -1;
				}
			}
			else
			{
				/* Server mode. Set up listening socket an accept a connection */
				listenfd = sockfd;
				sockfd = -1;
				if (bind(listenfd, iterator->ai_addr, iterator->ai_addrlen))
					goto sock_connect_exit;
				listen(listenfd, 1);
				sockfd = accept(listenfd, NULL, 0);
			}
		}
	}

sock_connect_exit:
	if (listenfd)
		close(listenfd);
	if (resolved_addr)
		freeaddrinfo(resolved_addr);
	if (sockfd < 0)
	{
		if (servername)
			fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		else
		{
			perror("server accept");
			fprintf(stderr, "accept() failed\n");
		}
	}
	return sockfd;
}
/******************************************************************************
* Function: sock_sync_data
*
* Input
* sock socket to transfer data on
* xfer_size size of data to transfer
* local_data pointer to data to be sent to remote
*
* Output
* remote_data pointer to buffer to receive remote data
*
* Returns
* 0 on success, negative error code on failure
*
* Description
* Sync data across a socket. The indicated local data will be sent to the
* remote. It will then wait for the remote to send its data back. It is
* assumed that the two sides are in sync and call this function in the proper
* order. Chaos will ensue if they are not. :)
*
sock_sync_data 函数在 RDMA 程序中通常用于同步控制信息，例如，在建立 RDMA 连接之前，两端可能需要交换队列对（QP）的信息。通过这种方式，每一端都能确保它们拥有进行 RDMA 通信所需的正确信息。
函数首先将本地数据（local_data）发送到远端，然后等待并接收远端发回的数据到 remote_data 缓冲区。
返回值为指向字符串的指针
* Also note this is a blocking function and will wait for the full data to be
* received from the remote.
*
******************************************************************************/
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
{
	int rc;
	int read_bytes = 0;
	int total_read_bytes = 0;
	rc = write(sock, local_data, xfer_size);
	if (rc < xfer_size)
		fprintf(stderr, "Failed writing data during sock_sync_data\n");
	else
		rc = 0;
	// ：使用 while 循环从套接字读取数据，直到读取到的总字节数等于预期的 xfer_size
	while (!rc && total_read_bytes < xfer_size)
	{
		read_bytes = read(sock, remote_data, xfer_size);
		if (read_bytes > 0)
			total_read_bytes += read_bytes;
		else
			rc = read_bytes;
	}
	return rc;
}
/******************************************************************************
End of socket operations
******************************************************************************/
/* poll_completion */
/******************************************************************************
* Function: poll_completion
*
* Input
* res pointer to resources structure
*
* Output
* none
无直接输出参数，但函数通过轮询 CQ 来获取 RDMA 操作的完成状态。
*
* Returns
* 0 on success, 1 on failure
*
* Description
* Poll the completion queue for a single event. This function will continue to
* poll the queue until MAX_POLL_CQ_TIMEOUT milliseconds have passed.
*
******************************************************************************/
int poll_completion(struct resources *res)
{
	// 定义并初始化用于轮询的变量，包括 struct ibv_wc wc（用于存储完成事件的详情）和时间相关的变量（用于控制轮询超时）
	struct ibv_wc wc;
	unsigned long start_time_msec;
	unsigned long cur_time_msec;
	struct timeval cur_time;
	int poll_result;
	int rc = 0;
	/* poll the completion for a while before giving up of doing it .. */
	gettimeofday(&cur_time, NULL);
	start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	do
	{
		poll_result = ibv_poll_cq(res->cq, 1, &wc);
		gettimeofday(&cur_time, NULL);
		cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	} while ((poll_result == 0) && ((cur_time_msec - start_time_msec) < MAX_POLL_CQ_TIMEOUT));

	if (poll_result < 0)
	{
		// 表示轮询 CQ 失败，打印错误消息，并设置返回代码为 1。
		fprintf(stderr, "poll CQ failed\n");
		rc = 1;
	}
	else if (poll_result == 0)
	{
		// 表示轮询超时但未找到完成事件，打印超时错误消息，并设置返回代码为 1。
		fprintf(stderr, "completion wasn't found in the CQ after timeout\n");
		rc = 1;
	}
	else
	{
		/* CQE found */
		fprintf(stdout, "completion was found in CQ with status 0x%x\n", wc.status);
		if (wc.status != IBV_WC_SUCCESS)
		{
			fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n", wc.status,
					wc.vendor_err);
			rc = 1;
		}
	}
	return rc;
}
/******************************************************************************
* Function: post_send，用于创建并提交一个发送工作请求（Send Work Request）到 RDMA 队列对（Queue Pair）

* Input：该函数接受一个指向资源结构体的指针和一个操作码，用于指定发送工作请求的类型。
* res pointer to resources structure
* opcode IBV_WR_SEND, IBV_WR_RDMA_READ or IBV_WR_RDMA_WRITE
*
* Output
* none
*
* Returns
* 0 on success, error code on failure
*
* Description
* This function will create and post a send work request
******************************************************************************/
int post_send(struct resources *res, int opcode)
{
	// 在 RDMA 操作中，发送工作请求用于指定如何发送数据（例如，普通发送、RDMA 读或写等）。
	// sr 的字段包括散布/聚集元素的列表、操作类型（opcode）、发送标志等
	struct ibv_send_wr sr;

	// sge 用于指定 RDMA 操作中要使用的数据缓冲区的地址、长度和本地密钥（lkey）。本地密钥是 RDMA 设备用于访问该内存区域的权限令牌。
	struct ibv_sge sge;

	// 如果 ibv_post_send 返回错误，bad_wr 将被设置为指向问题所在的发送工作请求。初始时设置为 NULL，表示没有错误。
	struct ibv_send_wr *bad_wr = NULL;
	int rc;
	memset(&sge, 0, sizeof(sge));	// 使用 memset 初始化散布/聚集条目 sge。
	sge.addr = (uintptr_t)res->buf; // 设置 sge.addr 为要发送或读写的数据的地址
	sge.length = MSG_SIZE;			// 设置 sge.length 为要发送或读写的数据的长度。
	sge.lkey = res->mr->lkey;		// 设置 sge.lkey 为关联内存区域的本地密钥。
	memset(&sr, 0, sizeof(sr));		// 使用 memset 初始化发送工作请求 sr。
	sr.next = NULL;
	sr.wr_id = 0;
	sr.sg_list = &sge;				   // 设置 sr.sg_list 指向散布/聚集条目
	sr.num_sge = 1;					   // 设置 sr.num_sge 为 1，表示只有一个散布/聚集条目。
	sr.opcode = opcode;				   // 设置 sr.opcode 为传入的操作码。
	sr.send_flags = IBV_SEND_SIGNALED; // 设置 sr.send_flags 为 IBV_SEND_SIGNALED，以触发完成事件。

	if (opcode != IBV_WR_SEND)
	{
		sr.wr.rdma.remote_addr = res->remote_props.addr;
		sr.wr.rdma.rkey = res->remote_props.rkey;
	}
	/* there is a Receive Request in the responder side, so we won't get any into RNR flow */
	// 在 post_send 函数中，rc 用于存储 ibv_post_send 函数的返回值，以指示操作是否成功。成功时，rc 通常为 0；失败时，它包含错误代码。
	rc = ibv_post_send(res->qp, &sr, &bad_wr);
	if (rc)
		fprintf(stderr, "failed to post SR\n");
	else
	{
		switch (opcode)
		{
		case IBV_WR_SEND:
			fprintf(stdout, "Send Request was posted\n");
			break;
		case IBV_WR_RDMA_READ:
			fprintf(stdout, "RDMA Read Request was posted\n");
			break;
		case IBV_WR_RDMA_WRITE:
			fprintf(stdout, "RDMA Write Request was posted\n");
			break;
		default:
			fprintf(stdout, "Unknown Request was posted\n");
			break;
		}
	}
	return rc;
}
/******************************************************************************
 * Function: post_receive
 * Input
 * res pointer to resources structure
 *  int post_receive(struct resources *res): 该函数接受一个指向包含 RDMA 资源的 resources 结构体的指针 res
 *
 * Output
 * none
 *
 * Returns
 * 0 on success, error code on failure
 *
 * Description
 *
 ******************************************************************************/
int post_receive(struct resources *res)
{
	struct ibv_recv_wr rr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	int rc;
	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)res->buf;
	sge.length = MSG_SIZE;
	sge.lkey = res->mr->lkey;

	memset(&rr, 0, sizeof(rr));
	rr.next = NULL;
	rr.wr_id = 0;
	rr.sg_list = &sge; // 设置 rr.sg_list 指向散布/聚集条目
	rr.num_sge = 1;	   // 设置 rr.num_sge 为 1，表示只有一个散布/聚集条目

	rc = ibv_post_recv(res->qp, &rr, &bad_wr);
	if (rc)
		fprintf(stderr, "failed to post RR\n");
	else
		fprintf(stdout, "Receive Request was posted\n");
	return rc;
}
/******************************************************************************
 * Function: resources_init
 *
 * Input
 * res pointer to resources structure
 *
 * Output
 * res is initialized
 *
 * Returns
 * none
 *
 * Description
 * res is initialized to default values
 ******************************************************************************/
void resources_init(struct resources *res)
{
	memset(res, 0, sizeof *res);
	// res->sock = -1;: 将 sock 成员（套接字文件描述符）设置为 -1。这是一个常用的技巧，用于表示该套接字尚未被分配或初始化
	res->sock = -1;
}
/******************************************************************************
* Function: resources_create
* Input
* res pointer to resources structure to be filled in
*
* Output
* res filled in with resources
*
* Returns
* 0 on success, 1 on failure
*
* Description
*
* This function creates and allocates all necessary system resources. These
* are stored in res.
通过正确创建和配置这些资源，RDMA 应用程序能够进行高效的网络通信和远程直接内存访问操作。
*****************************************************************************/
int resources_create(struct resources *res)
{

	// dev_list 是一个指向 InfiniBand 设备指针数组的指针。这个数组用于存储系统中检测到的所有 IB 设备
	// 初始设置为 NULL，这个数组将由 ibv_get_device_list 函数填充。
	struct ibv_device **dev_list = NULL;

	// qp_init_attr 是一个结构体，用于初始化队列对（Queue Pair, QP）。它包含了创建 QP 所需的所有参数，如 QP 类型、发送/接收完成队列（CQ）的指针、最大发送/接收工作请求等。
	struct ibv_qp_init_attr qp_init_attr;

	// ib_dev 是一个指向单个 IB 设备的指针。它将用于指向从 dev_list 中选定的设备
	// 最初设置为 NULL，在设备选择过程中会被赋值。
	struct ibv_device *ib_dev = NULL;

	// size 用于存储将要分配的内存缓冲区的大小。在这个上下文中，它通常被设置为消息大小。
	size_t size;

	// i 是一个循环计数器，用于遍历 IB 设备列表
	int i;

	// mr_flags 用于指定注册内存区域（Memory Region, MR）时的访问权限标志。这些标志包括本地写入、远程读取和远程写入权限。
	int mr_flags = 0;

	// cq_size 用于指定创建的完成队列（CQ）的大小。在这个示例中，由于每个端只发送一个工作请求，所以一个 CQ 条目足够了。
	int cq_size = 0;

	// num_devices 用于存储系统中检测到的 IB 设备数量。这个值由 ibv_get_device_list 函数设置。
	int num_devices;

	// rc 是一个返回码变量，用于存储函数的执行结果。成功时为 0，失败时为非零值。
	int rc = 0;

	// 根据配置，函数尝试建立一个 TCP 连接。在客户端模式下，它连接到指定的服务器和端口；在服务器模式下，它监听指定的端口。
	/* if client side */
	if (config.server_name)
	{
		res->sock = sock_connect(config.server_name, config.tcp_port);
		if (res->sock < 0)
		{
			fprintf(stderr, "failed to establish TCP connection to server %s, port %d\n",
					config.server_name, config.tcp_port);
			rc = -1;
			goto resources_create_exit;
		}
	}
	else
	{
		fprintf(stdout, "waiting on port %d for TCP connection\n", config.tcp_port);
		res->sock = sock_connect(NULL, config.tcp_port);
		if (res->sock < 0)
		{
			fprintf(stderr, "failed to establish TCP connection with client on port %d\n",
					config.tcp_port);
			rc = -1;
			goto resources_create_exit;
		}
	}
	fprintf(stdout, "TCP connection was established\n");
	fprintf(stdout, "searching for IB devices in host\n");

	// 使用 ibv_get_device_list 函数获取系统中所有 IB（InfiniBand）设备的列表
	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list)
	{
		fprintf(stderr, "failed to get IB devices list\n");
		rc = 1;
		goto resources_create_exit;
	}
	/* if there isn't any IB device in host */
	if (!num_devices)
	{
		fprintf(stderr, "found %d device(s)\n", num_devices);
		rc = 1;
		goto resources_create_exit;
	}
	fprintf(stdout, "found %d device(s)\n", num_devices);

	// 遍历设备列表，找到与配置中指定名称相匹配的设备。
	for (i = 0; i < num_devices; i++)
	{
		if (!config.dev_name)
		{
			// 自动选择设备列表中的第一个设备，并将其名称复制到 config.dev_name。这里使用 strdup 函数来分配新的内存并复制设备名称字符串。
			config.dev_name = strdup(ibv_get_device_name(dev_list[i]));
			fprintf(stdout, "device not specified, using first one found: %s\n", config.dev_name);
		}

		// 如果设备名称可以匹配
		if (!strcmp(ibv_get_device_name(dev_list[i]), config.dev_name))
		{
			// ib_dev = dev_list[i];：将 ib_dev 指针设置为匹配的设备。
			ib_dev = dev_list[i];
			break;
		}
	}
	/* if the device wasn't found in host */
	if (!ib_dev)
	{
		fprintf(stderr, "IB device %s wasn't found\n", config.dev_name);
		rc = 1;
		goto resources_create_exit;
	}

	// 使用 ibv_open_device 函数打开找到的设备，并获取设备上下文。
	res->ib_ctx = ibv_open_device(ib_dev);
	if (!res->ib_ctx)
	{
		fprintf(stderr, "failed to open device %s\n", config.dev_name);
		rc = 1;
		goto resources_create_exit;
	}
	// 现在初始化完毕，可以释放原来的设备列表了
	ibv_free_device_list(dev_list);
	dev_list = NULL;
	ib_dev = NULL;

	// 使用 ibv_query_port 查询指定 IB 端口的属性
	// 这个调用查询指定的 InfiniBand 端口属性，存储在 res->port_attr 中。
	// res->ib_ctx 是打开的 IB 设备的上下文，config.ib_port 是要查询的端口号。
	if (ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr))
	{
		fprintf(stderr, "ibv_query_port on port %u failed\n", config.ib_port);
		rc = 1;
		goto resources_create_exit;
	}

	// 使用 ibv_alloc_pd 分配一个保护域（Protection Domain）。
	res->pd = ibv_alloc_pd(res->ib_ctx);
	if (!res->pd)
	{
		fprintf(stderr, "ibv_alloc_pd failed\n");
		rc = 1;
		goto resources_create_exit;
	}

	// 使用 ibv_create_cq 创建一个完成队列（Completion Queue）。
	cq_size = 1;
	res->cq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
	if (!res->cq)
	{
		fprintf(stderr, "failed to create CQ with %u entries\n", cq_size);
		rc = 1;
		goto resources_create_exit;
	}

	// 分配内存缓冲区
	size = MSG_SIZE;
	res->buf = (char *)malloc(size);
	if (!res->buf)
	{
		fprintf(stderr, "failed to malloc %Zu bytes to memory buffer\n", size);
		rc = 1;
		goto resources_create_exit;
	}
	// // 使用 memset 将缓冲区清零。
	// memset(res->buf, 0, size);
	// // 如果是服务器端，将消息内容复制到缓冲区中。
	// if (!config.server_name)
	// {
	// 	printf("Enter your message: ");
	// 	if (fgets(res->buf, MSG_SIZE, stdin) != NULL) // 假设 BUFFER_SIZE 是 res.buf 的大小
	// 	{
	// 		// 除去可能的换行符
	// 		res->buf[strcspn(res->buf, "\n")] = 0;
	// 	}
	// 	else
	// 	{
	// 		fprintf(stderr, "Error reading input.\n");
	// 		// 可以选择如何处理输入错误
	// 	}
	// 	fprintf(stdout, "Server: going to send the message: '%s'\n", res->buf);
	// }
	// else
	memset(res->buf, 0, size);

	// 这行代码设定了用于注册内存区域的访问标志。IBV_ACCESS_LOCAL_WRITE 允许本地写入，IBV_ACCESS_REMOTE_READ 和 IBV_ACCESS_REMOTE_WRITE 分别允许远程端读取和写入这块内存。
	// 这些标志确保了内存区域既能被本地 RDMA 设备用于写操作，也能被远程 RDMA 设备用于读和写操作。
	mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	// 函数注册内存区域。这个调用关联了前面分配的保护域（res->pd）、内存缓冲区（res->buf）、缓冲区大小（size）以及访问标志（mr_flags）。
	res->mr = ibv_reg_mr(res->pd, res->buf, size, mr_flags);
	if (!res->mr)
	{
		fprintf(stderr, "ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
		rc = 1;
		goto resources_create_exit;
	}
	fprintf(stdout, "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
			res->buf, res->mr->lkey, res->mr->rkey, mr_flags);

	// 这一部分代码涉及使用 InfiniBand Verbs API 创建队列对（Queue Pair, QP），它是 RDMA 通信的核心组件。队列对包含两个队列：发送队列（Send Queue）和接收队列（Receive Queue）

	// 将 qp_init_attr 结构体的内容初始化为零。
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));

	// 设置队列对类型为可靠连接（Reliable Connection）。
	qp_init_attr.qp_type = IBV_QPT_RC;

	// 设置发送队列的所有工作请求在完成时都将产生一个完成事件。
	qp_init_attr.sq_sig_all = 1;

	// 指定发送和接收操作都使用同一个完成队列（Completion Queue）
	qp_init_attr.send_cq = res->cq;
	qp_init_attr.recv_cq = res->cq;

	// 这个字段指定了发送队列（Send Queue）可以容纳的最大工作请求（Work Request）数。在你的代码中，这个值被设置为 1，意味着发送队列一次只能容纳一个发送工作请求。
	qp_init_attr.cap.max_send_wr = 1;

	// 这个字段指定了接收队列（Receive Queue）可以容纳的最大工作请求数。在你的代码中，这个值也被设置为 1，意味着接收队列一次只能容纳一个接收工作请求。
	qp_init_attr.cap.max_recv_wr = 1;

	// : 设置每个工作请求的最大散布/聚集元素（Scatter/Gather Element）数为 1。
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	// 使用 ibv_create_qp 函数根据提供的属性创建队列对。
	res->qp = ibv_create_qp(res->pd, &qp_init_attr);
	if (!res->qp)
	{
		fprintf(stderr, "failed to create QP\n");
		rc = 1;
		goto resources_create_exit;
	}
	fprintf(stdout, "QP was created, QP number=0x%x\n", res->qp->qp_num);
resources_create_exit:
	// 这个资源清理过程确保了在发生错误时，所有已经分配或创建的资源被适当地释放，从而防止资源泄露。
	if (rc)
	{
		/* Error encountered, cleanup */
		if (res->qp)
		{
			ibv_destroy_qp(res->qp);
			res->qp = NULL;
		}
		if (res->mr)
		{
			ibv_dereg_mr(res->mr);
			res->mr = NULL;
		}
		if (res->buf)
		{
			free(res->buf);
			res->buf = NULL;
		}
		if (res->cq)
		{
			ibv_destroy_cq(res->cq);
			res->cq = NULL;
		}
		if (res->pd)
		{
			ibv_dealloc_pd(res->pd);
			res->pd = NULL;
		}
		if (res->ib_ctx)
		{
			ibv_close_device(res->ib_ctx);
			res->ib_ctx = NULL;
		}
		if (dev_list)
		{
			ibv_free_device_list(dev_list);
			dev_list = NULL;
		}
		if (res->sock >= 0)
		{
			if (close(res->sock))
				fprintf(stderr, "failed to close socket\n");
			res->sock = -1;
		}
	}
	return rc;
}
/******************************************************************************
 * Function: modify_qp_to_init
 *
 * Input
 * qp QP to transition
 *
 * Output
 * none
 *
 * Returns
 * 0 on success, ibv_modify_qp failure code on failure
 *
 * Description
 ******************************************************************************/
int modify_qp_to_init(struct ibv_qp *qp)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;
	memset(&attr, 0, sizeof(attr));

	// 设置队列对的目标状态为 INIT。
	attr.qp_state = IBV_QPS_INIT;

	//  设置队列对将要使用的端口号。
	attr.port_num = config.ib_port;

	// 置分区键（Partition Key）索引。在大多数情况下，这个值设置为 0。
	attr.pkey_index = 0;

	//  设置队列对的访问权限，包括本地写入、远程读取和远程写入。
	attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

	// 指定将要修改的队列对属性。
	flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

	// 函数修改队列对的状态。这个调用需要 qp、属性结构体 attr 和指定的标志 flags
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc)
		fprintf(stderr, "failed to modify QP state to INIT\n");
	return rc;
}
/******************************************************************************
 * Function: modify_qp_to_rtr
 *
 * Input
 * qp QP to transition
 * remote_qpn remote QP number
 * dlid destination LID
 * dgid destination GID (mandatory for RoCEE)
 *
 * Output
 * none
 *
 * Returns
 * 0 on success, ibv_modify_qp failure code on failure
 *
 * Description
 ******************************************************************************/
int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid)
{
	/*
	参数部分：
	qp: 要修改状态的队列对。
	remote_qpn: 远程队列对编号。
	dlid: 目的地局部标识符（Destination Local Identifier）。
	dgid: 目的地全局标识符（Destination Global Identifier），对 RoCEE（RDMA over Converged Ethernet）是必需的。
	*/

	struct ibv_qp_attr attr;
	int flags;
	int rc;
	memset(&attr, 0, sizeof(attr));

	// 设置队列对状态为 RTR (IBV_QPS_RTR)。
	attr.qp_state = IBV_QPS_RTR;

	// 设置路径最大传输单元（attr.path_mtu）
	attr.path_mtu = IBV_MTU_256;

	// 设置目的队列对编号（attr.dest_qp_num）为 remote_qpn。
	attr.dest_qp_num = remote_qpn;

	// 设置请求包序列号（attr.rq_psn）。
	attr.rq_psn = 0;

	// 设置目标端的最大远程读原子操作数（attr.max_dest_rd_atomic）。
	attr.max_dest_rd_atomic = 1;

	// 设置最小重试接收不足计时器（attr.min_rnr_timer）。
	attr.min_rnr_timer = 0x12;

	// 设置 attr.ah_attr 以定义队列对将要通信的物理路径属性。
	// : 表明这是一个局部通信，不使用全局路由头（Global Routing Header, GRH）。
	attr.ah_attr.is_global = 0;
	// 设置目的地局部标识符（Destination Local Identifier, DLID），这是 IB 网络中的一个重要参数，用于标识目的地端口。
	attr.ah_attr.dlid = dlid;
	// 设置服务级别（Service Level）。在大多数情况下，可以设置为 0。
	attr.ah_attr.sl = 0;
	// 设置源路径位，通常用于子网内的路径选择。
	attr.ah_attr.src_path_bits = 0;
	//  设置使用的 IB 端口号。
	attr.ah_attr.port_num = config.ib_port;

	// 如果使用全局标识符（GID），设置 attr.ah_attr.is_global 为 1 并复制 dgid 到 attr.ah_attr.grh.dgid。
	if (config.gid_idx >= 0)
	{
		// 如果 config.gid_idx 大于等于 0，表示需要使用全局标识符（GID）进行通信，这通常在跨子网通信时使用。

		// 设置为使用全局路由。
		attr.ah_attr.is_global = 1;
		// 通常在使用全局路由时，端口号被设置为 1。
		attr.ah_attr.port_num = 1;
		// 将目的地 GID 复制到地址句柄的全局路由头中。
		memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
		// 设置流标签，通常设置为 0
		attr.ah_attr.grh.flow_label = 0;
		// 设置跳数限制，对于 RDMA 通常设置为 1。
		attr.ah_attr.grh.hop_limit = 1;
		// 设置源 GID 索引
		attr.ah_attr.grh.sgid_index = config.gid_idx;
		// 设置流量类别，通常设置为 0。
		attr.ah_attr.grh.traffic_class = 0;
	}

	// ，指定将要修改的队列对属性。
	flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

	// 使用 ibv_modify_qp 函数根据指定的属性和标志修改队列对状态。
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc)
		fprintf(stderr, "failed to modify QP state to RTR\n");
	return rc;
}
/******************************************************************************
 * Function: modify_qp_to_rts
 *
 * Input
 * qp QP to transition
 *
 * Output
 * none
 *
 * Returns
 * 0 on success, ibv_modify_qp failure code on failure
 *
 * Description
函数的目的是将队列对（Queue Pair, QP）从准备接收（Ready to Receive, RTR）状态转换到准备发送（Ready to Send, RTS）状态。
 ******************************************************************************/
int modify_qp_to_rts(struct ibv_qp *qp)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;
	memset(&attr, 0, sizeof(attr));

	// 设置队列对的目标状态为 RTS。
	attr.qp_state = IBV_QPS_RTS;

	// 设置超时参数，用于确定重传超时时间。
	attr.timeout = 0x12;

	// 设置最大重试发送次数。
	attr.retry_cnt = 6;

	// 设置 RNR（Receiver Not Ready）重试次数。这里设置为 0 表示不进行 RNR 重试。
	attr.rnr_retry = 0;

	// 设置发送队列的包序列号。
	attr.sq_psn = 0;

	//  设置最大远程读原子操作数。
	attr.max_rd_atomic = 1;

	// 这些标志指定了要修改的队列对属性。
	flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
			IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

	// 使用 ibv_modify_qp 函数根据指定的属性和标志修改队列对状态。
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc)
		fprintf(stderr, "failed to modify QP state to RTS\n");
	return rc;
}
/******************************************************************************
 * Function: connect_qp
 *
 * Input
 * res pointer to resources structure
 *
 * Output
 * none
 *
 * Returns
 * 0 on success, error code on failure
 *
 * Description
 * Connect the QP. Transition the server side to RTR, sender side to RTS
 *
 * 连接队列对，将服务端变成待接受状态，客户端变成待发送状态
 * 函数的作用是配置和连接队列对（Queue Pair, QP），以便进行 RDMA 通信。这个过程包括设置队列对的状态，以及交换所需的连接信息。以下是函数的详细解释：
 ******************************************************************************/
int connect_qp(struct resources *res)
{

	// 这个结构体用于存储本地连接所需的信息，如本地队列对（QP）的编号、内存区域（MR）的键（key）、本地标识符（LID）和全局标识符（GID）。这些信息将被发送到远程端以建立连接。
	struct cm_con_data_t local_con_data;

	// 类似于 local_con_data，这个结构体用于存储从远程端接收的连接信息。在建立连接时，这些信息是必需的，例如，远程端的队列对编号和内存区域的键。
	struct cm_con_data_t remote_con_data;

	struct cm_con_data_t tmp_con_data;
	int rc = 0;

	// 这个字符变量通常用于同步过程中的简单数据交换，确保双方都准备好进行下一步操作
	char temp_char;

	// 这是一个全局标识符（Global Identifier, GID）的联合体，用于存储本地端的 GID。在使用 RoCE（RDMA over Converged Ethernet）或跨子网的 RDMA 通信时，GID 是必需的。它用于唯一标识 InfiniBand 网络中的设备。
	union ibv_gid my_gid;

	// 表示使用全局标识符（Global Identifier, GID）。函数查询并设置 GID
	if (config.gid_idx >= 0)
	{
		// 这行代码查询指定 IB 端口的 GID。res->ib_ctx 是设备上下文，config.ib_port 是端口号，config.gid_idx 是 GID 索引。
		rc = ibv_query_gid(res->ib_ctx, config.ib_port, config.gid_idx, &my_gid);
		if (rc)
		{
			fprintf(stderr, "could not get gid for port %d, index %d\n", config.ib_port, config.gid_idx);
			return rc;
		}
	}
	else
		fprintf(stdout, "using InfiniBand subnet connection\n");
	// 意味着不需要使用 GID。这种情况下，将 my_gid 清零。这通常用于仅在 InfiniBand 子网内通信的情况。
	memset(&my_gid, 0, sizeof my_gid);

	// 设置本地缓冲区地址。htonll 将地址从主机字节顺序转换为网络字节顺序。
	local_con_data.addr = htonll((uintptr_t)res->buf);
	// 设置本地内存区域（MR）的远程键（rkey）。htonl 转换为网络字节顺序。
	local_con_data.rkey = htonl(res->mr->rkey);
	//  设置本地队列对编号。同样使用 htonl 进行字节顺序转换。
	local_con_data.qp_num = htonl(res->qp->qp_num);
	// 设置本地标识符（LID）。htons 转换为网络字节顺序。
	local_con_data.lid = htons(res->port_attr.lid);
	// 复制 GID 到本地连接数据结构。
	memcpy(local_con_data.gid, &my_gid, 16);
	fprintf(stdout, "\nLocal LID = 0x%x\n", res->port_attr.lid);
	// 函数通过已建立的 TCP 套接字交换本地和远程连接数据。
	// 这里将远端的数据从socket里面读取然后放到临时数据中
	if (sock_sync_data(res->sock, sizeof(struct cm_con_data_t), (char *)&local_con_data, (char *)&tmp_con_data) < 0)
	{
		fprintf(stderr, "failed to exchange connection data between sides\n");
		rc = 1;
		goto connect_qp_exit;
	}

	// 从 tmp_con_data（临时存储远程数据）提取远程端的连接信息，转换回主机字节顺序，并存储在 remote_con_data。
	remote_con_data.addr = ntohll(tmp_con_data.addr);
	// 提取的数据包括远程缓冲区地址、远程 MR 的键、远程队列对编号和 LID。
	remote_con_data.rkey = ntohl(tmp_con_data.rkey);
	remote_con_data.qp_num = ntohl(tmp_con_data.qp_num);
	remote_con_data.lid = ntohs(tmp_con_data.lid);
	// 如果使用 GID，则从 tmp_con_data 复制 GID 到 remote_con_data。
	memcpy(remote_con_data.gid, tmp_con_data.gid, 16);
	/* save the remote side attributes, we will need it for the post SR */
	res->remote_props = remote_con_data;
	fprintf(stdout, "Remote address = 0x%" PRIx64 "\n", remote_con_data.addr);
	fprintf(stdout, "Remote rkey = 0x%x\n", remote_con_data.rkey);
	fprintf(stdout, "Remote QP number = 0x%x\n", remote_con_data.qp_num);
	fprintf(stdout, "Remote LID = 0x%x\n", remote_con_data.lid);
	// 如果使用 GID，也打印远程 GID
	if (config.gid_idx >= 0)
	{
		uint8_t *p = remote_con_data.gid;
		// 打印远程 GID 的每个字节：这个 GID 是一个 128 位的标识符，在这里以 16 个字节的形式打印出来，每个字节表示为两位十六进制数。
		fprintf(stdout, "Remote GID =%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n ", p[0],
				p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
	}

	// 将队列对的状态修改为 INIT。
	// 在这个阶段，队列对从其初始状态（RESET）转换到 INIT 状态。在 INIT 状态下，队列对被配置为具有必要的访问权限和网络参数，但还不能用于发送或接收数据。
	// 这是队列对生命周期中的第一个激活状态，为后续的数据传输做准备。
	rc = modify_qp_to_init(res->qp);
	if (rc)
	{
		fprintf(stderr, "change QP state to INIT failed\n");
		goto connect_qp_exit;
	}

	if (config.server_name)
	{
		rc = post_receive(res);
		if (rc)
		{
			fprintf(stderr, "failed to post RR\n");
			goto connect_qp_exit;
		}
	}

	// 在此状态下队列对开始准备接收远程端的数据。
	rc = modify_qp_to_rtr(res->qp, remote_con_data.qp_num, remote_con_data.lid, remote_con_data.gid);
	if (rc)
	{
		fprintf(stderr, "failed to modify QP state to RTR\n");
		goto connect_qp_exit;
	}

	rc = modify_qp_to_rts(res->qp);
	if (rc)
	{
		fprintf(stderr, "failed to modify QP state to RTR\n");
		goto connect_qp_exit;
	}
	fprintf(stdout, "QP state was change to RTS\n");

	if (sock_sync_data(res->sock, 1, "Q", &temp_char)) /* just send a dummy char back and forth */
	{
		fprintf(stderr, "sync error after QPs are were moved to RTS\n");
		rc = 1;
	}
connect_qp_exit:
	return rc;
}
/******************************************************************************
 * Function: resources_destroy
 *
 * Input
 * res pointer to resources structure
 *
 * Output
 * none
 *
 * Returns
 * 0 on success, 1 on failure
 *
 * Description
 * Cleanup and deallocate all resources used
 * 就是调用各个函数来释放空间和资源
 ******************************************************************************/
int resources_destroy(struct resources *res)
{
	int rc = 0;
	if (res->qp)
		if (ibv_destroy_qp(res->qp))
		{
			fprintf(stderr, "failed to destroy QP\n");
			rc = 1;
		}
	if (res->mr)
		if (ibv_dereg_mr(res->mr))
		{
			fprintf(stderr, "failed to deregister MR\n");
			rc = 1;
		}
	if (res->buf)
		free(res->buf);
	if (res->cq)
		if (ibv_destroy_cq(res->cq))
		{
			fprintf(stderr, "failed to destroy CQ\n");
			rc = 1;
		}
	if (res->pd)
		if (ibv_dealloc_pd(res->pd))
		{
			fprintf(stderr, "failed to deallocate PD\n");
			rc = 1;
		}
	if (res->ib_ctx)
		if (ibv_close_device(res->ib_ctx))
		{
			fprintf(stderr, "failed to close device context\n");
			rc = 1;
		}
	if (res->sock >= 0)
		if (close(res->sock))
		{
			fprintf(stderr, "failed to close socket\n");
			rc = 1;
		}
	return rc;
}
/******************************************************************************
 * Function: print_config
 *
 * Input
 * none
 *
 * Output
 * none
 *
 * Returns
 * none
 *
 * Description
 * Print out config information
 ******************************************************************************/
void print_config(void)
{
	fprintf(stdout, " ------------------------------------------------\n");
	fprintf(stdout, " Device name : \"%s\"\n", config.dev_name);
	fprintf(stdout, " IB port : %u\n", config.ib_port);
	if (config.server_name)
		fprintf(stdout, " IP : %s\n", config.server_name);
	fprintf(stdout, " TCP port : %u\n", config.tcp_port);
	if (config.gid_idx >= 0)
		fprintf(stdout, " GID index : %u\n", config.gid_idx);
	fprintf(stdout, " ------------------------------------------------\n\n");
}

/******************************************************************************
 * Function: usage
 *
 * Input
 * argv0 command line arguments
 *
 * Output
 * none
 *
 * Returns
 * none
 *
 * Description
 * print a description of command line syntax
 ******************************************************************************/
void usage(const char *argv0)
{
	fprintf(stdout, "Usage:\n");
	fprintf(stdout, " %s start a server and wait for connection\n", argv0);
	fprintf(stdout, " %s <host> connect to server at <host>\n", argv0);
	fprintf(stdout, "\n");
	fprintf(stdout, "Options:\n");
	fprintf(stdout, " -p, --port <port> listen on/connect to port <port> (default 18515)\n");
	fprintf(stdout, " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
	fprintf(stdout, " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
	fprintf(stdout, " -g, --gid_idx <git index> gid index to be used in GRH (default not used)\n");
}
/******************************************************************************
 * Function: receive_message
 *
 * Input
 * res pointer to resources structure where the message buffer is located
 * entity a string representing the entity (e.g., "Server", "Client") calling this function
 *
 * Output
 * Writes the received message into the res->buf buffer
 *
 * Returns
 * 0 if the loop should continue; 1 if an exit condition (like receiving "exit") occurs
 *
 * Description
 * Prompts the entity (Server/Client) to enter a message, receives the input,
 * and checks for an exit condition. The function reads the message into the
 * buffer provided in the resources structure (res->buf). If the message is "exit",
 * the function returns 1, signaling the caller to terminate the process.
 ******************************************************************************/
int receive_message(struct resources *res, const char *entity)
{
	printf("%s: Enter your message (type 'exit' to end): ", entity);
	if (fgets(res->buf, MSG_SIZE, stdin) == NULL || strcmp(res->buf, "exit\n") == 0)
	{
		return 1; // return 1 indicates exit
	}
	// 除去可能的换行符
	res->buf[strcspn(res->buf, "\n")] = 0;
	return 0; // return 0 indicates continue
}
