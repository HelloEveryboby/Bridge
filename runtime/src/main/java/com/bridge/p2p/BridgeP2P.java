package com.bridge.p2p;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Bridge P2P 统一多语言点到点通信协议 (Java 实现)
 * 零第三方依赖，纯 Java Socket 实现。
 */
public class BridgeP2P {
    public static final byte[] MAGIC = new byte[]{'B', 'R', 'G', '1'};
    public static final int MAX_PAYLOAD = 16 * 1024 * 1024;

    public interface RpcHandler {
        String handle(String method, String paramsJson) throws Exception;
    }

    public interface EventHandler {
        void onEvent(String topic, String dataJson);
    }

    // ================== Frame I/O ==================
    public static void sendFrame(OutputStream out, String jsonStr) throws IOException {
        byte[] payload = jsonStr.getBytes(StandardCharsets.UTF_8);
        if (payload.length > MAX_PAYLOAD) {
            throw new IOException("Payload exceeds maximum size: " + payload.length);
        }
        DataOutputStream dos = new DataOutputStream(out);
        dos.write(MAGIC);
        dos.writeInt(payload.length);
        dos.write(payload);
        dos.flush();
    }

    public static String readFrame(InputStream in) throws IOException {
        DataInputStream dis = new DataInputStream(in);
        byte[] magic = new byte[4];
        dis.readFully(magic);
        if (magic[0] != 'B' || magic[1] != 'R' || magic[2] != 'G' || magic[3] != '1') {
            throw new IOException("Invalid protocol magic");
        }
        int len = dis.readInt();
        if (len < 0 || len > MAX_PAYLOAD) {
            throw new IOException("Invalid frame length: " + len);
        }
        byte[] payload = new byte[len];
        dis.readFully(payload);
        return new String(payload, StandardCharsets.UTF_8);
    }

    // ================== Minimal JSON Helpers ==================
    public static String extractJsonField(String json, String key) {
        String pattern = "\"" + key + "\"";
        int idx = json.indexOf(pattern);
        if (idx == -1) return null;
        int colon = json.indexOf(':', idx + pattern.length());
        if (colon == -1) return null;
        int start = colon + 1;
        while (start < json.length() && Character.isWhitespace(json.charAt(start))) start++;
        if (start >= json.length()) return null;

        char c = json.charAt(start);
        if (c == '"') {
            int end = start + 1;
            while (end < json.length()) {
                if (json.charAt(end) == '"' && json.charAt(end - 1) != '\\') break;
                end++;
            }
            if (end < json.length()) {
                return json.substring(start + 1, end);
            }
        } else if (c == '{' || c == '[') {
            char open = c;
            char close = (open == '{') ? '}' : ']';
            int depth = 1;
            int end = start + 1;
            while (end < json.length() && depth > 0) {
                if (json.charAt(end) == open) depth++;
                else if (json.charAt(end) == close) depth--;
                end++;
            }
            return json.substring(start, end);
        } else {
            int end = start;
            while (end < json.length() && json.charAt(end) != ',' && json.charAt(end) != '}' && json.charAt(end) != ']') {
                end++;
            }
            return json.substring(start, end).trim();
        }
        return null;
    }

    // ================== P2P Server ==================
    public static class Server {
        private final String host;
        private final int port;
        private ServerSocket serverSocket;
        private volatile boolean running = false;
        private final Map<String, RpcHandler> rpcHandlers = new ConcurrentHashMap<>();
        private final Map<String, EventHandler> eventHandlers = new ConcurrentHashMap<>();
        private final List<Socket> clients = new CopyOnWriteArrayList<>();
        private final ExecutorService threadPool = Executors.newCachedThreadPool();

        public Server(String uri) {
            URI u = URI.create(uri);
            this.host = (u.getHost() == null) ? "127.0.0.1" : u.getHost();
            this.port = (u.getPort() == -1) ? 9001 : u.getPort();
        }

        public Server registerRpc(String method, RpcHandler handler) {
            rpcHandlers.put(method, handler);
            return this;
        }

        public Server onEvent(String topic, EventHandler handler) {
            eventHandlers.put(topic, handler);
            return this;
        }

        public void publish(String topic, String dataJson) {
            String msg = "{\"type\":\"event\",\"topic\":\"" + topic + "\",\"data\":" + (dataJson == null ? "{}" : dataJson) + "}";
            for (Socket s : clients) {
                try {
                    synchronized (s) {
                        sendFrame(s.getOutputStream(), msg);
                    }
                } catch (Exception ignored) {
                }
            }
        }

        public Server start(boolean async) throws IOException {
            serverSocket = new ServerSocket(port, 64, InetAddress.getByName(host));
            running = true;
            Runnable r = () -> {
                while (running) {
                    try {
                        Socket client = serverSocket.accept();
                        client.setTcpNoDelay(true);
                        clients.add(client);
                        threadPool.submit(() -> handleClient(client));
                    } catch (Exception e) {
                        break;
                    }
                }
            };
            if (async) {
                new Thread(r, "BridgeP2P-Server").start();
            } else {
                r.run();
            }
            return this;
        }

        private void handleClient(Socket client) {
            try (InputStream in = client.getInputStream();
                 OutputStream out = client.getOutputStream()) {
                while (running && !client.isClosed()) {
                    String frame = readFrame(in);
                    String type = extractJsonField(frame, "type");
                    if ("rpc_req".equals(type)) {
                        String id = extractJsonField(frame, "id");
                        String method = extractJsonField(frame, "method");
                        String params = extractJsonField(frame, "params");
                        RpcHandler h = rpcHandlers.get(method);
                        String response;
                        if (h != null) {
                            try {
                                String res = h.handle(method, params);
                                response = "{\"type\":\"rpc_res\",\"id\":\"" + id + "\",\"result\":" + (res == null ? "null" : res) + "}";
                            } catch (Exception ex) {
                                response = "{\"type\":\"rpc_res\",\"id\":\"" + id + "\",\"error\":{\"code\":-32000,\"message\":\"" + ex.getMessage() + "\"}}";
                            }
                        } else {
                            response = "{\"type\":\"rpc_res\",\"id\":\"" + id + "\",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
                        }
                        synchronized (client) {
                            sendFrame(out, response);
                        }
                    } else if ("event".equals(type)) {
                        String topic = extractJsonField(frame, "topic");
                        String data = extractJsonField(frame, "data");
                        EventHandler eh = eventHandlers.get(topic);
                        if (eh == null) eh = eventHandlers.get("*");
                        if (eh != null) {
                            try {
                                eh.onEvent(topic, data);
                            } catch (Exception ignored) {}
                        }
                    } else if ("ping".equals(type)) {
                        synchronized (client) {
                            sendFrame(out, "{\"type\":\"pong\"}");
                        }
                    }
                }
            } catch (Exception ignored) {
            } finally {
                clients.remove(client);
                try {
                    client.close();
                } catch (Exception ignored) {}
            }
        }

        public void stop() {
            running = false;
            try {
                if (serverSocket != null) serverSocket.close();
            } catch (Exception ignored) {}
            for (Socket s : clients) {
                try {
                    s.close();
                } catch (Exception ignored) {}
            }
            clients.clear();
            threadPool.shutdownNow();
        }
    }

    // ================== P2P Client ==================
    public static class Client {
        private final Socket socket;
        private final InputStream in;
        private final OutputStream out;
        private volatile boolean running = true;
        private final AtomicInteger reqIdSeq = new AtomicInteger(1);
        private final Map<String, CompletableFuture<String>> pendingRequests = new ConcurrentHashMap<>();
        private final Map<String, EventHandler> eventHandlers = new ConcurrentHashMap<>();

        public Client(String uri) throws IOException {
            URI u = URI.create(uri);
            String host = (u.getHost() == null) ? "127.0.0.1" : u.getHost();
            int port = (u.getPort() == -1) ? 9001 : u.getPort();
            socket = new Socket(host, port);
            socket.setTcpNoDelay(true);
            in = socket.getInputStream();
            out = socket.getOutputStream();

            Thread reader = new Thread(this::recvLoop, "BridgeP2P-ClientRecv");
            reader.setDaemon(true);
            reader.start();
        }

        private void recvLoop() {
            while (running && !socket.isClosed()) {
                try {
                    String frame = readFrame(in);
                    String type = extractJsonField(frame, "type");
                    if ("rpc_res".equals(type)) {
                        String id = extractJsonField(frame, "id");
                        CompletableFuture<String> future = pendingRequests.remove(id);
                        if (future != null) {
                            future.complete(frame);
                        }
                    } else if ("event".equals(type)) {
                        String topic = extractJsonField(frame, "topic");
                        String data = extractJsonField(frame, "data");
                        EventHandler h = eventHandlers.get(topic);
                        if (h == null) h = eventHandlers.get("*");
                        if (h != null) {
                            h.onEvent(topic, data);
                        }
                    }
                } catch (Exception e) {
                    break;
                }
            }
            // cancel all pending
            for (CompletableFuture<String> f : pendingRequests.values()) {
                f.completeExceptionally(new IOException("Connection closed"));
            }
            pendingRequests.clear();
        }

        public String call(String method, String paramsJson, long timeoutMs) throws Exception {
            String reqId = "java-" + reqIdSeq.getAndIncrement();
            CompletableFuture<String> future = new CompletableFuture<>();
            pendingRequests.put(reqId, future);

            String req = "{\"type\":\"rpc_req\",\"id\":\"" + reqId + "\",\"method\":\"" + method + "\",\"params\":" + (paramsJson == null ? "{}" : paramsJson) + "}";
            synchronized (out) {
                sendFrame(out, req);
            }

            String resFrame = future.get(timeoutMs, TimeUnit.MILLISECONDS);
            String err = extractJsonField(resFrame, "error");
            if (err != null) {
                throw new RuntimeException("RPC Error: " + err);
            }
            return extractJsonField(resFrame, "result");
        }

        public void publish(String topic, String dataJson) throws IOException {
            String msg = "{\"type\":\"event\",\"topic\":\"" + topic + "\",\"data\":" + (dataJson == null ? "{}" : dataJson) + "}";
            synchronized (out) {
                sendFrame(out, msg);
            }
        }

        public void subscribe(String topic, EventHandler handler) {
            eventHandlers.put(topic, handler);
        }

        public void close() {
            running = false;
            try {
                socket.close();
            } catch (Exception ignored) {}
        }
    }
}
