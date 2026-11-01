import com.bridge.p2p.BridgeP2P;

public class JavaEchoService {
    public static void main(String[] args) throws Exception {
        String uri = (args.length > 0) ? args[0] : "tcp://127.0.0.1:20001";
        BridgeP2P.Server server = new BridgeP2P.Server(uri);

        // 注册 Java 端的 RPC 计算方法
        server.registerRpc("java.compute", (method, paramsJson) -> {
            String valStr = BridgeP2P.extractJsonField(paramsJson, "x");
            double x = Double.parseDouble(valStr);
            return "{\"result\":" + (x * 10) + ",\"from\":\"java_backend\"}";
        });

        // 监听来自其他语言后端的事件
        server.onEvent("cross_lang_event", (topic, dataJson) -> {
            System.out.println("[Java Service] Received event: " + topic + " -> " + dataJson);
            // 广播回一条通知
            server.publish("java_ack", "{\"status\":\"processed_by_java\"}");
        });

        System.out.println("[Java Service] Starting on " + uri + "...");
        server.start(false); // blocking
    }
}
