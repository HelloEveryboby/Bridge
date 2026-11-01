import unittest
import time
import sys
import os

# Add repo python dir to sys.path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from bridge_p2p import P2PServer, P2PClient

class TestBridgeP2PPython(unittest.TestCase):
    def test_tcp_rpc_and_event(self):
        uri = "tcp://127.0.0.1:19991"
        server = P2PServer(uri)

        @server.register("greet")
        def greet(name: str):
            return f"Hello, {name}!"

        @server.register("calc.multiply")
        def multiply(x: int, y: int):
            return x * y

        server_events = []
        @server.on_event("client_event")
        def on_client_event(topic, data):
            server_events.append(data)

        server.start(blocking=False)
        time.sleep(0.05)

        client = P2PClient(uri)

        # RPC Test
        res = client.call("greet", {"name": "World"})
        self.assertEqual(res, "Hello, World!")

        res_calc = client.call("calc.multiply", [6, 7])
        self.assertEqual(res_calc, 42)

        # Event Test (Client -> Server)
        client.publish("client_event", {"status": "ok", "code": 200})
        time.sleep(0.05)
        self.assertEqual(len(server_events), 1)
        self.assertEqual(server_events[0]["code"], 200)

        # Event Test (Server -> Client)
        client_events = []
        client.subscribe("server_broadcast", lambda t, d: client_events.append(d))

        server.publish("server_broadcast", {"broadcast": "all systems normal"})
        time.sleep(0.05)
        self.assertEqual(len(client_events), 1)
        self.assertEqual(client_events[0]["broadcast"], "all systems normal")

        client.close()
        server.stop()

    def test_unix_socket_rpc(self):
        sock_path = "/tmp/test_bridge_p2p.sock"
        uri = f"unix://{sock_path}"
        server = P2PServer(uri)

        server.register_rpc("echo", lambda text: text)
        server.start(blocking=False)
        time.sleep(0.05)

        client = P2PClient(uri)
        res = client.call("echo", "p2p_socket_works")
        self.assertEqual(res, "p2p_socket_works")

        client.close()
        server.stop()

if __name__ == "__main__":
    unittest.main()
