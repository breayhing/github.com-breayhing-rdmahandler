
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define MAX_POLL_CQ_TIMEOUT 2000
#define MSG "******************************************************************************/"
#define MSG_SIZE (strlen(MSG) + 6)
#if __BYTE_ORDER == __LITTLE_ENDIAN

static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

struct config_t
{
    const char *dev_name; /* IB device name */
    char *server_name;    /* server host name */
    u_int32_t tcp_port;   /* server TCP port */
    int ib_port;          // 本地使用的 InfiniBand 端口号
    int gid_idx;          // 用于选择要使用的全局唯一标识符（Global Identifier，GID）的索引
};

struct cm_con_data_t
{
    uint64_t addr;         // 缓冲区的内存地址。
    uint32_t rkey;         // 远程密钥，用于远程访问 RDMA 缓冲区。
    uint32_t qp_num;       // 队列对的编号。
    uint16_t lid;          // 本地 InfiniBand 端口的本地标识符（Local Identifier）
    uint8_t gid[16];       /* gid */
} __attribute__((packed)); 

struct resources
{
    struct ibv_device_attr
        device_attr;
    struct ibv_port_attr port_attr;    /* InfiniBand 端口的属性*/
    struct cm_con_data_t remote_props; /*存储用于连接远程端的值。 */
    struct ibv_context *ib_ctx;        /*指向 InfiniBand 设备上下文的指针 */
    struct ibv_pd *pd;                 /* 保护域（Protection Domain）的句柄。*/
    struct ibv_cq *cq;                 /* 完成队列（Completion Queue）的句柄 */
    struct ibv_qp *qp;                 /* 队列对的句柄。*/
    struct ibv_mr *mr;                 /* 指向用于 RDMA 操作的内存区域（Memory Region）的句柄。 */
    char *buf;                         /* 用于 RDMA 和发送操作的内存缓冲区指针 */
    int sock;                          /* TCP 套接字的文件描述符。 */
};
extern struct config_t config;

int sock_connect(const char *servername, int port);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);
int poll_completion(struct resources *res);
int post_send(struct resources *res, int opcode);
int post_receive(struct resources *res);
void resources_init(struct resources *res);
int resources_create(struct resources *res);
int modify_qp_to_init(struct ibv_qp *qp);
int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid);
int modify_qp_to_rts(struct ibv_qp *qp);
int connect_qp(struct resources *res);
int resources_destroy(struct resources *res);
void print_config(void);
void usage(const char *argv0);
int receive_message(struct resources *res, const char *entity);
