#include "bridge_p2p.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

static int g_event_received = 0;
static char g_last_event_data[256] = {0};

/* 简单的加法 RPC 实现: params: {"a": 10, "b": 20} -> {"sum": 30} */
static char* rpc_add_handler(const char* method, const char* params_json, void* user_data, int* is_error) {
    int a = 0, b = 0;
    if (params_json) {
        const char* pa = strstr(params_json, "\"a\":");
        const char* pb = strstr(params_json, "\"b\":");
        if (pa) a = atoi(pa + 4);
        if (pb) b = atoi(pb + 4);
    }
    char* res = (char*)malloc(64);
    snprintf(res, 64, "{\"sum\":%d}", a + b);
    *is_error = 0;
    return res;
}

static void on_test_event(const char* topic, const char* data_json, void* user_data) {
    g_event_received++;
    if (data_json) {
        strncpy(g_last_event_data, data_json, sizeof(g_last_event_data) - 1);
    }
}

int main(void) {
    printf("=== Testing Bridge P2P C Core ===\n");

    const char* uri = "tcp://127.0.0.1:19876";
    P2PServer* server = p2p_server_create(uri);
    assert(server != NULL);

    int rc = p2p_server_register_rpc(server, "math.add", rpc_add_handler, NULL);
    assert(rc == 0);

    rc = p2p_server_register_event(server, "notice", on_test_event, NULL);
    assert(rc == 0);

    rc = p2p_server_start_async(server);
    assert(rc == 0);
    usleep(50000); // 50ms wait for server startup

    // Client connection
    P2PClient* client = p2p_client_connect(uri, 1000);
    assert(client != NULL);

    // Test RPC Call
    char err_buf[256] = {0};
    char* result = p2p_client_call(client, "math.add", "{\"a\":15,\"b\":27}", 2000, err_buf, sizeof(err_buf));
    if (!result) {
        printf("RPC Call Failed: %s\n", err_buf);
        return 1;
    }
    printf("RPC math.add result: %s\n", result);
    assert(strstr(result, "\"sum\":42") != NULL);
    free(result);

    // Test Event Publishing from Client to Server
    p2p_client_publish(client, "notice", "{\"msg\":\"hello from client\"}");
    usleep(50000); // wait for event processing
    assert(g_event_received == 1);
    assert(strstr(g_last_event_data, "hello from client") != NULL);
    printf("Event received successfully: %s\n", g_last_event_data);

    // Cleanup
    p2p_client_destroy(client);
    p2p_server_destroy(server);

    printf("=== All Bridge P2P C Core Tests Passed! ===\n");
    return 0;
}
