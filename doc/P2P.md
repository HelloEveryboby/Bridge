# Bridge P2P: 多语言后端无中间件点到点互通协议规范

## 1. 核心目标与设计理念
- **点到点直连 (Peer-to-Peer / Brokerless)**：任意两个多语言后端服务直接建立 TCP 或 Unix Domain Socket (UDS) 连接，无需部署任何外部 Broker（如 Redis、RabbitMQ、Nginx）。
- **零配置 (Zero-Config)**：无需独立配置文件，仅需一行 URI（如 `tcp://127.0.0.1:9001` 或 `unix:///tmp/bridge.sock`）即可启动服务或建立连接。
- **无需 IDL 预编译 (IDL-Free)**：使用基于自描述 JSON 的消息报文格式，支持原生复杂数据结构传递，无需像 Protobuf/Thrift 那样编译 `.proto` 文件。
- **统一双模型**：
  1. **RPC（请求-应答）**：`client.call(method, params, timeout_ms)` 返回结果或异常。
  2. **事件/消息（Pub-Sub / Event）**：`client.publish(topic, data)` 与 `server.subscribe(topic, handler)`。

---

## 2. 传输层协议定义 (Framing Protocol)

采用长度前缀（Length-Prefixed Frame）传输报文：

```
+-------------------+------------------------------------+
| 4 字节魔数/类型标记 | 4 字节数据长度 (Big-Endian uint32) |
| (0x42 0x52 0x47 0x31 "BRG1") | Payload Length (N bytes)   |
+-------------------+------------------------------------+
| N 字节 JSON 负载 (UTF-8 编码的 JSON 字符串)                 |
+--------------------------------------------------------+
```

### 帧结构说明：
1. **Magic Header** (4 bytes): 固定 ASCII 字符 `BRG1` (`0x42, 0x52, 0x47, 0x31`)，用于协议校验与防伪。
2. **Payload Length** (4 bytes): 32位网络大端无符号整数（Big-Endian uint32），表示后续 JSON 负载的字节数。
3. **Payload** (N bytes): UTF-8 编码的 JSON 数据。

---

## 3. 消息报文格式 (Message Formats)

### 3.1 RPC 请求 (RPC Request)
```json
{
  "type": "rpc_req",
  "id": "req-1001",
  "method": "user.get_profile",
  "params": {"user_id": 42}
}
```

### 3.2 RPC 响应 (RPC Response)
- **成功**：
```json
{
  "type": "rpc_res",
  "id": "req-1001",
  "result": {"user_id": 42, "name": "Alice", "role": "admin"}
}
```
- **失败**：
```json
{
  "type": "rpc_res",
  "id": "req-1001",
  "error": {
    "code": -32601,
    "message": "Method 'user.get_profile' not found"
  }
}
```

### 3.3 事件推送 (Event / Pub-Sub)
```json
{
  "type": "event",
  "topic": "order.created",
  "data": {"order_id": 9988, "amount": 199.9}
}
```

### 3.4 心跳 / Ping-Pong (可选)
```json
{"type": "ping"}
{"type": "pong"}
```

---

## 4. 地址 URI 规范
- **TCP 连接**：`tcp://<host>:<port>` （例如 `tcp://127.0.0.1:9527` 或 `tcp://0.0.0.0:9527`）
- **Unix Domain Socket**：`unix://<path>` （例如 `unix:///tmp/bridge_p2p.sock`）
