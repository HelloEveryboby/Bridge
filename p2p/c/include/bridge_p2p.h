#ifndef BRIDGE_P2P_H
#define BRIDGE_P2P_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define BRIDGE_P2P_MAGIC "BRG1"
#define BRIDGE_P2P_MAGIC_LEN 4
#define BRIDGE_P2P_MAX_PAYLOAD (16 * 1024 * 1024) /* 16MB max message */

/* 协议消息类型 */
typedef enum {
    P2P_MSG_RPC_REQ = 1,
    P2P_MSG_RPC_RES = 2,
    P2P_MSG_EVENT   = 3,
    P2P_MSG_PING    = 4,
    P2P_MSG_PONG    = 5,
    P2P_MSG_UNKNOWN = 0
} P2PMsgType;

/* 回调函数类型定义 */
/* RPC 处理器：接收 (method, params_json, user_data)，返回动态分配的 result_json 或 error_json 字符串（调用方负责 free） */
typedef char* (*P2PRpcHandler)(const char* method, const char* params_json, void* user_data, int* is_error);

/* 事件监听器：接收 (topic, data_json, user_data) */
typedef void (*P2PEventHandler)(const char* topic, const char* data_json, void* user_data);

/* ==================== Server API ==================== */
typedef struct P2PServer P2PServer;

/* 创建 Server 实例并监听 (支持 "tcp://ip:port" 或 "unix:///path/to/sock") */
P2PServer* p2p_server_create(const char* uri);

/* 注册 RPC 处理函数 */
int p2p_server_register_rpc(P2PServer* server, const char* method, P2PRpcHandler handler, void* user_data);

/* 注册事件处理函数 */
int p2p_server_register_event(P2PServer* server, const char* topic, P2PEventHandler handler, void* user_data);

/* 广播/推送事件给所有连接到此 Server 的客户端 */
int p2p_server_publish(P2PServer* server, const char* topic, const char* data_json);

/* 启动 Server 事件循环 (blocking) */
int p2p_server_start(P2PServer* server);

/* 启动 Server 在后台线程运行 (non-blocking) */
int p2p_server_start_async(P2PServer* server);

/* 停止 Server 并释放资源 */
void p2p_server_stop(P2PServer* server);
void p2p_server_destroy(P2PServer* server);


/* ==================== Client API ==================== */
typedef struct P2PClient P2PClient;

/* 创建并连接到服务端 (支持 "tcp://ip:port" 或 "unix:///path/to/sock") */
P2PClient* p2p_client_connect(const char* uri, int timeout_ms);

/* 发起点对点同步 RPC 调用 */
/* 返回动态分配的 result_json 字符串，必须调用 free() 释放。失败返回 NULL 并可通过 err_buf 获取错误 */
char* p2p_client_call(P2PClient* client, const char* method, const char* params_json, int timeout_ms, char* err_buf, size_t err_buf_len);

/* 向服务端推送/发布单向事件 */
int p2p_client_publish(P2PClient* client, const char* topic, const char* data_json);

/* 客户端订阅事件 */
int p2p_client_subscribe(P2PClient* client, const char* topic, P2PEventHandler handler, void* user_data);

/* 关闭并销毁 Client */
void p2p_client_close(P2PClient* client);
void p2p_client_destroy(P2PClient* client);

/* ==================== 底层帧封包工具 ==================== */
/* 发送一个带 BRG1 头和长度前缀的 JSON 帧，成功返回 0，失败返回 -1 */
int p2p_send_frame(int fd, const char* json_str);

/* 读取一个完整的 JSON 帧，返回动态分配的 JSON 字符串（调用方需 free），失败返回 NULL */
char* p2p_read_frame(int fd, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_P2P_H */
