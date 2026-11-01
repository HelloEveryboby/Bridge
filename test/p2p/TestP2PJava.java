import com.bridge.p2p.BridgeP2P;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public class TestP2PJava {
    public static void main(String[] args) throws Exception {
        System.out.println("=== Testing Bridge P2P Java SDK ===");
        String uri = "tcp://127.0.0.1:19992";

        // 1. Start Server
        BridgeP2P.Server server = new BridgeP2P.Server(uri);
        server.registerRpc("calc.power", (method, paramsJson) -> {
            // Simple parsing
            String baseStr = BridgeP2P.extractJsonField(paramsJson, "base");
            String expStr = BridgeP2P.extractJsonField(paramsJson, "exp");
            double base = Double.parseDouble(baseStr);
            double exp = Double.parseDouble(expStr);
            double result = Math.pow(base, exp);
            return "{\"val\":" + result + "}";
        });

        CountDownLatch serverEventLatch = new CountDownLatch(1);
        AtomicReference<String> serverReceivedData = new AtomicReference<>();
        server.onEvent("java.event", (topic, data) -> {
            serverReceivedData.set(data);
            serverEventLatch.countDown();
        });

        server.start(true);
        Thread.sleep(100);

        // 2. Connect Client
        BridgeP2P.Client client = new BridgeP2P.Client(uri);

        // 3. RPC Call Test
        String res = client.call("calc.power", "{\"base\":2,\"exp\":10}", 2000);
        System.out.println("RPC calc.power response: " + res);
        String val = BridgeP2P.extractJsonField(res, "val");
        if (!"1024.0".equals(val) && !"1024".equals(val)) {
            throw new AssertionError("Expected 1024.0, got " + val);
        }

        // 4. Event Test (Client -> Server)
        client.publish("java.event", "{\"sender\":\"testClient\",\"msg\":\"hello server\"}");
        boolean ok = serverEventLatch.await(2, TimeUnit.SECONDS);
        if (!ok) {
            throw new AssertionError("Server did not receive event in time");
        }
        System.out.println("Server received event: " + serverReceivedData.get());
        if (!serverReceivedData.get().contains("hello server")) {
            throw new AssertionError("Unexpected event data: " + serverReceivedData.get());
        }

        // 5. Event Test (Server -> Client)
        CountDownLatch clientEventLatch = new CountDownLatch(1);
        AtomicReference<String> clientReceivedData = new AtomicReference<>();
        client.subscribe("broadcast", (topic, data) -> {
            clientReceivedData.set(data);
            clientEventLatch.countDown();
        });

        server.publish("broadcast", "{\"announcement\":\"system alert\"}");
        ok = clientEventLatch.await(2, TimeUnit.SECONDS);
        if (!ok) {
            throw new AssertionError("Client did not receive broadcast event in time");
        }
        System.out.println("Client received broadcast: " + clientReceivedData.get());

        client.close();
        server.stop();
        System.out.println("=== All Bridge P2P Java SDK Tests Passed! ===");
    }
}
