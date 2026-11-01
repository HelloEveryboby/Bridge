#define _GNU_SOURCE
#include "bridge_p2p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <stdbool.h>

/* ================= 辅助工具函数 ================= */

static int wait_fd_readable(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
        return 0;
    }
    return (ret == 0) ? -2 : -1; /* -2: timeout, -1: error */
}

static int wait_fd_writable(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLOUT)) {
        return 0;
    }
    return (ret == 0) ? -2 : -1;
}

static ssize_t write_all(int fd, const void* buf, size_t count) {
    size_t written = 0;
    const uint8_t* ptr = (const uint8_t*)buf;
    while (written < count) {
        ssize_t n = write(fd, ptr + written, count - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (wait_fd_writable(fd, 5000) != 0) return -1;
                continue;
            }
            return -1;
        }
        if (n == 0) return written;
        written += n;
    }
    return written;
}

static ssize_t read_exact(int fd, void* buf, size_t count, int timeout_ms) {
    size_t total = 0;
    uint8_t* ptr = (uint8_t*)buf;
    while (total < count) {
        if (timeout_ms >= 0) {
            int ret = wait_fd_readable(fd, timeout_ms);
            if (ret != 0) return -1;
        }
        ssize_t n = read(fd, ptr + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (timeout_ms >= 0) {
                    int ret = wait_fd_readable(fd, timeout_ms);
                    if (ret != 0) return -1;
                    continue;
                }
            }
            return -1;
        }
        if (n == 0) return (total == 0) ? 0 : -1; /* EOF */
        total += n;
    }
    return total;
}

/* ================= 报文传输层 (Framing) ================= */

int p2p_send_frame(int fd, const char* json_str) {
    if (fd < 0 || !json_str) return -1;
    uint32_t len = (uint32_t)strlen(json_str);
    if (len > BRIDGE_P2P_MAX_PAYLOAD) return -1;

    uint8_t header[8];
    memcpy(header, BRIDGE_P2P_MAGIC, 4);
    header[4] = (uint8_t)((len >> 24) & 0xFF);
    header[5] = (uint8_t)((len >> 16) & 0xFF);
    header[6] = (uint8_t)((len >> 8) & 0xFF);
    header[7] = (uint8_t)(len & 0xFF);

    if (write_all(fd, header, 8) != 8) return -1;
    if (write_all(fd, json_str, len) != (ssize_t)len) return -1;
    return 0;
}

char* p2p_read_frame(int fd, int timeout_ms) {
    if (fd < 0) return NULL;
    uint8_t header[8];
    ssize_t n = read_exact(fd, header, 8, timeout_ms);
    if (n != 8) return NULL;

    if (memcmp(header, BRIDGE_P2P_MAGIC, 4) != 0) {
        return NULL;
    }

    uint32_t len = ((uint32_t)header[4] << 24) |
                   ((uint32_t)header[5] << 16) |
                   ((uint32_t)header[6] << 8)  |
                   ((uint32_t)header[7]);

    if (len > BRIDGE_P2P_MAX_PAYLOAD) return NULL;

    char* payload = (char*)malloc(len + 1);
    if (!payload) return NULL;

    n = read_exact(fd, payload, len, timeout_ms);
    if (n != (ssize_t)len) {
        free(payload);
        return NULL;
    }
    payload[len] = '\0';
    return payload;
}

/* 简单的 JSON 解析工具函数（免去第三方库依赖） */
static char* json_extract_string(const char* json, const char* key) {
    if (!json || !key) return NULL;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (*p != '\"') return NULL;
    p++;
    const char* end = p;
    while (*end && *end != '\"') {
        if (*end == '\\' && *(end + 1)) end += 2;
        else end++;
    }
    if (*end != '\"') return NULL;
    size_t len = end - p;
    char* res = (char*)malloc(len + 1);
    memcpy(res, p, len);
    res[len] = '\0';
    return res;
}

static char* json_extract_field_raw(const char* json, const char* key) {
    if (!json || !key) return NULL;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (!*p) return NULL;

    const char* end = p;
    if (*p == '{') {
        int depth = 1;
        end++;
        while (*end && depth > 0) {
            if (*end == '{') depth++;
            else if (*end == '}') depth--;
            end++;
        }
    } else if (*p == '[') {
        int depth = 1;
        end++;
        while (*end && depth > 0) {
            if (*end == '[') depth++;
            else if (*end == ']') depth--;
            end++;
        }
    } else if (*p == '\"') {
        end++;
        while (*end && *end != '\"') {
            if (*end == '\\' && *(end + 1)) end += 2;
            else end++;
        }
        if (*end == '\"') end++;
    } else {
        while (*end && *end != ',' && *end != '}' && *end != ']' && *end != ' ' && *end != '\n' && *end != '\r') end++;
    }

    size_t len = end - p;
    char* res = (char*)malloc(len + 1);
    memcpy(res, p, len);
    res[len] = '\0';
    return res;
}

/* ==================== 解析 URI ==================== */
static int parse_uri_and_connect(const char* uri) {
    if (!uri) return -1;
    if (strncmp(uri, "tcp://", 6) == 0) {
        const char* hp = uri + 6;
        char host[128];
        int port = 0;
        const char* colon = strchr(hp, ':');
        if (!colon) return -1;
        size_t hlen = colon - hp;
        if (hlen >= sizeof(host)) return -1;
        memcpy(host, hp, hlen);
        host[hlen] = '\0';
        port = atoi(colon + 1);

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
            close(fd);
            return -1;
        }

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            close(fd);
            return -1;
        }
        return fd;
    } else if (strncmp(uri, "unix://", 7) == 0) {
        const char* path = uri + 7;
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            close(fd);
            return -1;
        }
        return fd;
    }
    return -1;
}

static int parse_uri_and_bind(const char* uri, char* unix_path, size_t unix_path_len) {
    if (!uri) return -1;
    if (unix_path) unix_path[0] = '\0';

    if (strncmp(uri, "tcp://", 6) == 0) {
        const char* hp = uri + 6;
        char host[128];
        int port = 0;
        const char* colon = strchr(hp, ':');
        if (!colon) return -1;
        size_t hlen = colon - hp;
        if (hlen >= sizeof(host)) return -1;
        memcpy(host, hp, hlen);
        host[hlen] = '\0';
        port = atoi(colon + 1);

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (strcmp(host, "0.0.0.0") == 0 || strcmp(host, "*") == 0) {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            inet_pton(AF_INET, host, &addr.sin_addr);
        }

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            close(fd);
            return -1;
        }
        listen(fd, 64);
        return fd;
    } else if (strncmp(uri, "unix://", 7) == 0) {
        const char* path = uri + 7;
        if (unix_path) strncpy(unix_path, path, unix_path_len - 1);
        unlink(path);

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            close(fd);
            return -1;
        }
        listen(fd, 64);
        return fd;
    }
    return -1;
}

/* ==================== Server 实现 ==================== */

#define MAX_RPC_HANDLERS 128
#define MAX_EVENT_HANDLERS 128
#define MAX_SERVER_CLIENTS 64

typedef struct {
    char method[128];
    P2PRpcHandler handler;
    void* user_data;
} RpcRegistry;

typedef struct {
    char topic[128];
    P2PEventHandler handler;
    void* user_data;
} EventRegistry;

struct P2PServer {
    char uri[256];
    char unix_path[256];
    int listen_fd;
    volatile bool running;
    pthread_t thread;

    pthread_mutex_t lock;
    RpcRegistry rpc_handlers[MAX_RPC_HANDLERS];
    int rpc_count;
    EventRegistry event_handlers[MAX_EVENT_HANDLERS];
    int event_count;

    int client_fds[MAX_SERVER_CLIENTS];
    int client_count;
};

P2PServer* p2p_server_create(const char* uri) {
    if (!uri) return NULL;
    P2PServer* s = (P2PServer*)calloc(1, sizeof(P2PServer));
    if (!s) return NULL;

    strncpy(s->uri, uri, sizeof(s->uri) - 1);
    pthread_mutex_init(&s->lock, NULL);

    s->listen_fd = parse_uri_and_bind(uri, s->unix_path, sizeof(s->unix_path));
    if (s->listen_fd < 0) {
        pthread_mutex_destroy(&s->lock);
        free(s);
        return NULL;
    }
    s->running = false;
    return s;
}

int p2p_server_register_rpc(P2PServer* server, const char* method, P2PRpcHandler handler, void* user_data) {
    if (!server || !method || !handler) return -1;
    pthread_mutex_lock(&server->lock);
    if (server->rpc_count >= MAX_RPC_HANDLERS) {
        pthread_mutex_unlock(&server->lock);
        return -1;
    }
    strncpy(server->rpc_handlers[server->rpc_count].method, method, 127);
    server->rpc_handlers[server->rpc_count].handler = handler;
    server->rpc_handlers[server->rpc_count].user_data = user_data;
    server->rpc_count++;
    pthread_mutex_unlock(&server->lock);
    return 0;
}

int p2p_server_register_event(P2PServer* server, const char* topic, P2PEventHandler handler, void* user_data) {
    if (!server || !topic || !handler) return -1;
    pthread_mutex_lock(&server->lock);
    if (server->event_count >= MAX_EVENT_HANDLERS) {
        pthread_mutex_unlock(&server->lock);
        return -1;
    }
    strncpy(server->event_handlers[server->event_count].topic, topic, 127);
    server->event_handlers[server->event_count].handler = handler;
    server->event_handlers[server->event_count].user_data = user_data;
    server->event_count++;
    pthread_mutex_unlock(&server->lock);
    return 0;
}

int p2p_server_publish(P2PServer* server, const char* topic, const char* data_json) {
    if (!server || !topic) return -1;
    const char* d = data_json ? data_json : "{}";
    size_t payload_len = strlen(topic) + strlen(d) + 64;
    char* buf = (char*)malloc(payload_len);
    if (!buf) return -1;
    snprintf(buf, payload_len, "{\"type\":\"event\",\"topic\":\"%s\",\"data\":%s}", topic, d);

    pthread_mutex_lock(&server->lock);
    for (int i = 0; i < server->client_count; i++) {
        p2p_send_frame(server->client_fds[i], buf);
    }
    pthread_mutex_unlock(&server->lock);
    free(buf);
    return 0;
}

static void handle_server_msg(P2PServer* server, int client_fd, const char* json_msg) {
    char* type = json_extract_string(json_msg, "type");
    if (!type) return;

    if (strcmp(type, "rpc_req") == 0) {
        char* id = json_extract_string(json_msg, "id");
        char* method = json_extract_string(json_msg, "method");
        char* params = json_extract_field_raw(json_msg, "params");

        P2PRpcHandler handler = NULL;
        void* user_data = NULL;

        pthread_mutex_lock(&server->lock);
        for (int i = 0; i < server->rpc_count; i++) {
            if (strcmp(server->rpc_handlers[i].method, method ? method : "") == 0) {
                handler = server->rpc_handlers[i].handler;
                user_data = server->rpc_handlers[i].user_data;
                break;
            }
        }
        pthread_mutex_unlock(&server->lock);

        char res_buf[4096];
        if (handler) {
            int is_error = 0;
            char* out = handler(method, params, user_data, &is_error);
            if (!out) out = strdup("null");
            if (is_error) {
                snprintf(res_buf, sizeof(res_buf),
                         "{\"type\":\"rpc_res\",\"id\":\"%s\",\"error\":%s}",
                         id ? id : "", out);
            } else {
                snprintf(res_buf, sizeof(res_buf),
                         "{\"type\":\"rpc_res\",\"id\":\"%s\",\"result\":%s}",
                         id ? id : "", out);
            }
            free(out);
        } else {
            snprintf(res_buf, sizeof(res_buf),
                     "{\"type\":\"rpc_res\",\"id\":\"%s\",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}",
                     id ? id : "");
        }
        p2p_send_frame(client_fd, res_buf);

        if (id) free(id);
        if (method) free(method);
        if (params) free(params);
    } else if (strcmp(type, "event") == 0) {
        char* topic = json_extract_string(json_msg, "topic");
        char* data = json_extract_field_raw(json_msg, "data");

        pthread_mutex_lock(&server->lock);
        for (int i = 0; i < server->event_count; i++) {
            if (strcmp(server->event_handlers[i].topic, topic ? topic : "") == 0 ||
                strcmp(server->event_handlers[i].topic, "*") == 0) {
                P2PEventHandler h = server->event_handlers[i].handler;
                void* ud = server->event_handlers[i].user_data;
                pthread_mutex_unlock(&server->lock);
                h(topic, data, ud);
                pthread_mutex_lock(&server->lock);
            }
        }
        pthread_mutex_unlock(&server->lock);

        if (topic) free(topic);
        if (data) free(data);
    } else if (strcmp(type, "ping") == 0) {
        p2p_send_frame(client_fd, "{\"type\":\"pong\"}");
    }

    free(type);
}

typedef struct {
    P2PServer* server;
    int client_fd;
} ClientWorkerArg;

static void* client_worker_loop(void* arg) {
    ClientWorkerArg* cwa = (ClientWorkerArg*)arg;
    P2PServer* server = cwa->server;
    int cfd = cwa->client_fd;
    free(cwa);

    while (server->running) {
        char* frame = p2p_read_frame(cfd, 100);
        if (frame) {
            handle_server_msg(server, cfd, frame);
            free(frame);
        } else {
            if (errno != 0 && errno != EAGAIN && errno != ETIMEDOUT && errno != EINTR) {
                /* Connection closed or error */
                break;
            }
        }
    }

    close(cfd);
    pthread_mutex_lock(&server->lock);
    for (int i = 0; i < server->client_count; i++) {
        if (server->client_fds[i] == cfd) {
            server->client_fds[i] = server->client_fds[server->client_count - 1];
            server->client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&server->lock);
    return NULL;
}

int p2p_server_start(P2PServer* server) {
    if (!server || server->listen_fd < 0) return -1;
    server->running = true;

    while (server->running) {
        int ret = wait_fd_readable(server->listen_fd, 200);
        if (ret == 0) {
            int cfd = accept(server->listen_fd, NULL, NULL);
            if (cfd >= 0) {
                pthread_mutex_lock(&server->lock);
                if (server->client_count < MAX_SERVER_CLIENTS) {
                    server->client_fds[server->client_count++] = cfd;
                    pthread_mutex_unlock(&server->lock);

                    ClientWorkerArg* cwa = (ClientWorkerArg*)malloc(sizeof(ClientWorkerArg));
                    cwa->server = server;
                    cwa->client_fd = cfd;
                    pthread_t t;
                    pthread_create(&t, NULL, client_worker_loop, cwa);
                    pthread_detach(t);
                } else {
                    pthread_mutex_unlock(&server->lock);
                    close(cfd);
                }
            }
        }
    }
    return 0;
}

static void* server_thread_func(void* arg) {
    P2PServer* server = (P2PServer*)arg;
    p2p_server_start(server);
    return NULL;
}

int p2p_server_start_async(P2PServer* server) {
    if (!server) return -1;
    server->running = true;
    return pthread_create(&server->thread, NULL, server_thread_func, server);
}

void p2p_server_stop(P2PServer* server) {
    if (!server) return;
    server->running = false;
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    if (server->thread) {
        pthread_join(server->thread, NULL);
        server->thread = 0;
    }
    if (server->unix_path[0]) {
        unlink(server->unix_path);
    }
}

void p2p_server_destroy(P2PServer* server) {
    if (!server) return;
    p2p_server_stop(server);
    pthread_mutex_destroy(&server->lock);
    free(server);
}

/* ==================== Client 实现 ==================== */

typedef struct PendingRpc {
    char id[64];
    char* result_json;
    bool done;
    bool error;
    pthread_cond_t cond;
    struct PendingRpc* next;
} PendingRpc;

struct P2PClient {
    char uri[256];
    int fd;
    volatile bool running;
    pthread_t recv_thread;
    pthread_mutex_t lock;

    uint32_t req_counter;
    PendingRpc* pending_head;

    EventRegistry event_handlers[MAX_EVENT_HANDLERS];
    int event_count;
};

static void* client_recv_loop(void* arg) {
    P2PClient* client = (P2PClient*)arg;
    while (client->running) {
        char* frame = p2p_read_frame(client->fd, 100);
        if (frame) {
            char* type = json_extract_string(frame, "type");
            if (type && strcmp(type, "rpc_res") == 0) {
                char* id = json_extract_string(frame, "id");
                char* result = json_extract_field_raw(frame, "result");
                char* err = json_extract_field_raw(frame, "error");

                pthread_mutex_lock(&client->lock);
                PendingRpc* cur = client->pending_head;
                while (cur) {
                    if (id && strcmp(cur->id, id) == 0) {
                        cur->result_json = err ? err : result;
                        cur->error = (err != NULL);
                        cur->done = true;
                        pthread_cond_signal(&cur->cond);
                        break;
                    }
                    cur = cur->next;
                }
                pthread_mutex_unlock(&client->lock);

                if (id) free(id);
                if (err && result) free(result);
            } else if (type && strcmp(type, "event") == 0) {
                char* topic = json_extract_string(frame, "topic");
                char* data = json_extract_field_raw(frame, "data");

                pthread_mutex_lock(&client->lock);
                for (int i = 0; i < client->event_count; i++) {
                    if (strcmp(client->event_handlers[i].topic, topic ? topic : "") == 0 ||
                        strcmp(client->event_handlers[i].topic, "*") == 0) {
                        P2PEventHandler h = client->event_handlers[i].handler;
                        void* ud = client->event_handlers[i].user_data;
                        pthread_mutex_unlock(&client->lock);
                        h(topic, data, ud);
                        pthread_mutex_lock(&client->lock);
                    }
                }
                pthread_mutex_unlock(&client->lock);

                if (topic) free(topic);
                if (data) free(data);
            }
            if (type) free(type);
            free(frame);
        } else {
            if (errno != 0 && errno != EAGAIN && errno != ETIMEDOUT && errno != EINTR) {
                break;
            }
        }
    }
    return NULL;
}

P2PClient* p2p_client_connect(const char* uri, int timeout_ms) {
    (void)timeout_ms;
    if (!uri) return NULL;
    int fd = parse_uri_and_connect(uri);
    if (fd < 0) return NULL;

    P2PClient* c = (P2PClient*)calloc(1, sizeof(P2PClient));
    if (!c) {
        close(fd);
        return NULL;
    }
    strncpy(c->uri, uri, sizeof(c->uri) - 1);
    c->fd = fd;
    c->running = true;
    c->req_counter = 1;
    pthread_mutex_init(&c->lock, NULL);

    pthread_create(&c->recv_thread, NULL, client_recv_loop, c);
    return c;
}

char* p2p_client_call(P2PClient* client, const char* method, const char* params_json, int timeout_ms, char* err_buf, size_t err_buf_len) {
    if (!client || !method) {
        if (err_buf) snprintf(err_buf, err_buf_len, "Invalid client or method");
        return NULL;
    }
    pthread_mutex_lock(&client->lock);
    char req_id[64];
    snprintf(req_id, sizeof(req_id), "req-%u", client->req_counter++);

    PendingRpc item;
    memset(&item, 0, sizeof(item));
    strncpy(item.id, req_id, sizeof(item.id) - 1);
    pthread_cond_init(&item.cond, NULL);

    item.next = client->pending_head;
    client->pending_head = &item;
    pthread_mutex_unlock(&client->lock);

    const char* p = params_json ? params_json : "{}";
    size_t send_len = strlen(req_id) + strlen(method) + strlen(p) + 64;
    char* send_buf = (char*)malloc(send_len);
    snprintf(send_buf, send_len, "{\"type\":\"rpc_req\",\"id\":\"%s\",\"method\":\"%s\",\"params\":%s}", req_id, method, p);

    if (p2p_send_frame(client->fd, send_buf) != 0) {
        free(send_buf);
        pthread_mutex_lock(&client->lock);
        // remove item
        if (client->pending_head == &item) client->pending_head = item.next;
        else {
            PendingRpc* cur = client->pending_head;
            while (cur && cur->next != &item) cur = cur->next;
            if (cur) cur->next = item.next;
        }
        pthread_mutex_unlock(&client->lock);
        pthread_cond_destroy(&item.cond);
        if (err_buf) snprintf(err_buf, err_buf_len, "Failed to send frame");
        return NULL;
    }
    free(send_buf);

    // wait for response
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&client->lock);
    while (!item.done && client->running) {
        int rc = pthread_cond_timedwait(&item.cond, &client->lock, &ts);
        if (rc == ETIMEDOUT) break;
    }

    // unlink
    if (client->pending_head == &item) {
        client->pending_head = item.next;
    } else {
        PendingRpc* cur = client->pending_head;
        while (cur && cur->next != &item) cur = cur->next;
        if (cur) cur->next = item.next;
    }
    pthread_mutex_unlock(&client->lock);
    pthread_cond_destroy(&item.cond);

    if (!item.done) {
        if (err_buf) snprintf(err_buf, err_buf_len, "RPC Call timeout after %d ms", timeout_ms);
        return NULL;
    }
    if (item.error) {
        if (err_buf) snprintf(err_buf, err_buf_len, "RPC Error: %s", item.result_json ? item.result_json : "unknown");
        if (item.result_json) free(item.result_json);
        return NULL;
    }

    return item.result_json;
}

int p2p_client_publish(P2PClient* client, const char* topic, const char* data_json) {
    if (!client || !topic) return -1;
    const char* d = data_json ? data_json : "{}";
    size_t len = strlen(topic) + strlen(d) + 64;
    char* buf = (char*)malloc(len);
    snprintf(buf, len, "{\"type\":\"event\",\"topic\":\"%s\",\"data\":%s}", topic, d);
    int ret = p2p_send_frame(client->fd, buf);
    free(buf);
    return ret;
}

int p2p_client_subscribe(P2PClient* client, const char* topic, P2PEventHandler handler, void* user_data) {
    if (!client || !topic || !handler) return -1;
    pthread_mutex_lock(&client->lock);
    if (client->event_count >= MAX_EVENT_HANDLERS) {
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    strncpy(client->event_handlers[client->event_count].topic, topic, 127);
    client->event_handlers[client->event_count].handler = handler;
    client->event_handlers[client->event_count].user_data = user_data;
    client->event_count++;
    pthread_mutex_unlock(&client->lock);
    return 0;
}

void p2p_client_close(P2PClient* client) {
    if (!client) return;
    client->running = false;
    if (client->fd >= 0) {
        shutdown(client->fd, SHUT_RDWR);
        close(client->fd);
        client->fd = -1;
    }
    if (client->recv_thread) {
        pthread_join(client->recv_thread, NULL);
        client->recv_thread = 0;
    }
}

void p2p_client_destroy(P2PClient* client) {
    if (!client) return;
    p2p_client_close(client);
    pthread_mutex_destroy(&client->lock);
    free(client);
}
