import socket
import struct
import json
import threading
import time
import os
from urllib.parse import urlparse
from typing import Callable, Any, Dict, Optional

MAGIC = b"BRG1"
MAX_PAYLOAD = 16 * 1024 * 1024

def _parse_uri(uri: str):
    parsed = urlparse(uri)
    if parsed.scheme == "tcp":
        return ("tcp", parsed.hostname or "127.0.0.1", parsed.port or 9001)
    elif parsed.scheme == "unix":
        path = parsed.path
        if not path and parsed.netloc:
            path = "/" + parsed.netloc
        return ("unix", path, 0)
    else:
        raise ValueError(f"Unsupported URI scheme in {uri}")

def send_frame(sock: socket.socket, data: dict):
    payload = json.dumps(data, ensure_ascii=False).encode("utf-8")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("Payload size exceeds maximum allowed")
    header = MAGIC + struct.pack(">I", len(payload))
    sock.sendall(header + payload)

def read_frame(sock: socket.socket) -> Optional[dict]:
    header = _recv_all(sock, 8)
    if not header:
        return None
    if header[:4] != MAGIC:
        raise ValueError(f"Invalid magic header: {header[:4]}")
    length = struct.unpack(">I", header[4:8])[0]
    if length > MAX_PAYLOAD:
        raise ValueError(f"Payload too large: {length}")
    payload = _recv_all(sock, length)
    if not payload:
        return None
    return json.loads(payload.decode("utf-8"))

def _recv_all(sock: socket.socket, n: int) -> Optional[bytes]:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)

class P2PServer:
    def __init__(self, uri: str):
        self.uri = uri
        self.proto, self.host_or_path, self.port = _parse_uri(uri)
        self.sock: Optional[socket.socket] = None
        self.rpc_handlers: Dict[str, Callable] = {}
        self.event_handlers: Dict[str, Callable] = {}
        self.clients = []
        self._lock = threading.Lock()
        self.running = False
        self._thread: Optional[threading.Thread] = None

    def register(self, method: Optional[str] = None):
        """装饰器或直接注册 RPC 处理函数"""
        def decorator(fn):
            m = method or fn.__name__
            self.rpc_handlers[m] = fn
            return fn
        if callable(method):
            fn = method
            self.rpc_handlers[fn.__name__] = fn
            return fn
        return decorator

    def register_rpc(self, method: str, handler: Callable):
        self.rpc_handlers[method] = handler

    def on_event(self, topic: str, handler: Optional[Callable] = None):
        if handler is not None:
            self.event_handlers[topic] = handler
            return handler
        def decorator(fn):
            self.event_handlers[topic] = fn
            return fn
        return decorator

    def publish(self, topic: str, data: Any):
        """向所有连接的客户端推送事件"""
        msg = {"type": "event", "topic": topic, "data": data}
        with self._lock:
            dead_clients = []
            for client in self.clients:
                try:
                    send_frame(client, msg)
                except Exception:
                    dead_clients.append(client)
            for dc in dead_clients:
                self.clients.remove(dc)

    def start(self, blocking: bool = True):
        if self.proto == "tcp":
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.bind((self.host_or_path, self.port))
        else:
            if os.path.exists(self.host_or_path):
                os.unlink(self.host_or_path)
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.bind(self.host_or_path)

        self.sock.listen(64)
        self.running = True

        if blocking:
            self._serve()
        else:
            self._thread = threading.Thread(target=self._serve, daemon=True)
            self._thread.start()

    def _serve(self):
        while self.running:
            try:
                client_sock, _ = self.sock.accept()
                with self._lock:
                    self.clients.append(client_sock)
                threading.Thread(target=self._client_handler, args=(client_sock,), daemon=True).start()
            except Exception:
                break

    def _client_handler(self, client_sock: socket.socket):
        while self.running:
            try:
                frame = read_frame(client_sock)
                if frame is None:
                    break
                mtype = frame.get("type")
                if mtype == "rpc_req":
                    req_id = frame.get("id")
                    method = frame.get("method")
                    params = frame.get("params")
                    handler = self.rpc_handlers.get(method)
                    if handler:
                        try:
                            if isinstance(params, list):
                                res = handler(*params)
                            elif isinstance(params, dict):
                                res = handler(**params)
                            elif params is None:
                                res = handler()
                            else:
                                res = handler(params)
                            send_frame(client_sock, {"type": "rpc_res", "id": req_id, "result": res})
                        except Exception as e:
                            send_frame(client_sock, {"type": "rpc_res", "id": req_id, "error": {"code": -32000, "message": str(e)}})
                    else:
                        send_frame(client_sock, {"type": "rpc_res", "id": req_id, "error": {"code": -32601, "message": f"Method {method} not found"}})
                elif mtype == "event":
                    topic = frame.get("topic")
                    data = frame.get("data")
                    h = self.event_handlers.get(topic) or self.event_handlers.get("*")
                    if h:
                        try:
                            h(topic, data)
                        except Exception:
                            pass
                elif mtype == "ping":
                    send_frame(client_sock, {"type": "pong"})
            except Exception:
                break

        with self._lock:
            if client_sock in self.clients:
                self.clients.remove(client_sock)
        try:
            client_sock.close()
        except Exception:
            pass

    def stop(self):
        self.running = False
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None
        with self._lock:
            for c in self.clients:
                try:
                    c.close()
                except Exception:
                    pass
            self.clients.clear()
        if self.proto == "unix" and os.path.exists(self.host_or_path):
            try:
                os.unlink(self.host_or_path)
            except Exception:
                pass

class P2PClient:
    def __init__(self, uri: str, timeout: float = 5.0):
        self.uri = uri
        self.proto, self.host_or_path, self.port = _parse_uri(uri)
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None
        self.running = False
        self._thread: Optional[threading.Thread] = None
        self._req_id = 0
        self._lock = threading.Lock()
        self._pending_rpc: Dict[str, dict] = {}
        self.event_handlers: Dict[str, Callable] = {}
        self._connect()

    def _connect(self):
        if self.proto == "tcp":
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.sock.connect((self.host_or_path, self.port))
        else:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(self.host_or_path)

        self.running = True
        self._thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._thread.start()

    def _recv_loop(self):
        while self.running:
            try:
                frame = read_frame(self.sock)
                if frame is None:
                    break
                mtype = frame.get("type")
                if mtype == "rpc_res":
                    req_id = frame.get("id")
                    with self._lock:
                        fut = self._pending_rpc.get(req_id)
                        if fut:
                            fut["response"] = frame
                            fut["event"].set()
                elif mtype == "event":
                    topic = frame.get("topic")
                    data = frame.get("data")
                    h = self.event_handlers.get(topic) or self.event_handlers.get("*")
                    if h:
                        try:
                            h(topic, data)
                        except Exception:
                            pass
            except Exception:
                break

    def call(self, method: str, params: Any = None, timeout: Optional[float] = None) -> Any:
        with self._lock:
            self._req_id += 1
            req_id = f"py-{self._req_id}"
            fut = {"event": threading.Event(), "response": None}
            self._pending_rpc[req_id] = fut

        msg = {
            "type": "rpc_req",
            "id": req_id,
            "method": method,
            "params": params
        }
        send_frame(self.sock, msg)

        wait_time = timeout or self.timeout
        if not fut["event"].wait(wait_time):
            with self._lock:
                self._pending_rpc.pop(req_id, None)
            raise TimeoutError(f"RPC call '{method}' timed out after {wait_time}s")

        with self._lock:
            self._pending_rpc.pop(req_id, None)

        res_frame = fut["response"]
        if "error" in res_frame:
            err = res_frame["error"]
            raise RuntimeError(f"RPC Error [{err.get('code')}]: {err.get('message')}")
        return res_frame.get("result")

    def publish(self, topic: str, data: Any):
        msg = {"type": "event", "topic": topic, "data": data}
        send_frame(self.sock, msg)

    def subscribe(self, topic: str, handler: Callable):
        self.event_handlers[topic] = handler

    def close(self):
        self.running = False
        if self.sock:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
                self.sock.close()
            except Exception:
                pass
            self.sock = None
