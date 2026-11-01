import sys
import os
import time
import subprocess

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../../python")))
from bridge_p2p import P2PClient, P2PServer

def test_cross_lang_e2e():
    print("==================================================")
    print(" Running Cross-Language P2P End-to-End Tests")
    print("==================================================")

    # 1. 编译 JavaEchoService
    subprocess.check_call([
        "javac", "-d", "build/p2p_classes",
        "runtime/src/main/java/com/bridge/p2p/BridgeP2P.java",
        "test/p2p_e2e/JavaEchoService.java"
    ])

    java_uri = "tcp://127.0.0.1:20001"
    java_proc = subprocess.Popen([
        "java", "-cp", "build/p2p_classes", "JavaEchoService", java_uri
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    try:
        time.sleep(0.5)

        # ====== Test A: Python Client -> Java Server (RPC & Events) ======
        print("[Test 1] Python Client connecting directly to Java Server via TCP...")
        py_client = P2PClient(java_uri)

        # Call Java RPC
        res = py_client.call("java.compute", {"x": 4.2})
        print(f" -> Python received from Java RPC: {res}")
        assert res.get("result") == 42.0
        assert res.get("from") == "java_backend"
        print(" -> [PASS] Python -> Java RPC invocation successful!")

        # Event: Java broadcast -> Python listener
        java_ack_received = []
        py_client.subscribe("java_ack", lambda t, d: java_ack_received.append(d))

        # Event: Python -> Java
        print("[Test 2] Python sending point-to-point event to Java Server...")
        py_client.publish("cross_lang_event", {"task": "process_order", "id": 101})
        time.sleep(0.3)
        assert len(java_ack_received) >= 1
        print(f" -> Python received Java ack event: {java_ack_received[0]}")
        assert java_ack_received[0].get("status") == "processed_by_java"
        print(" -> [PASS] Bidirectional event delivery between Python & Java successful!")

        py_client.close()

        # ====== Test B: Node.js Client -> Java Server (RPC) ======
        print("[Test 3] Node.js Client connecting directly to Java Server...")
        js_code = f"""
        const {{ BridgeP2PClient }} = require('./p2p/js/index.js');
        async function run() {{
            const c = new BridgeP2PClient('{java_uri}');
            await c.connect();
            const res = await c.call('java.compute', {{ x: 7 }});
            console.log('NodeJS got result:', JSON.stringify(res));
            if (res.result !== 70) process.exit(1);
            c.close();
        }}
        run().catch(err => {{ console.error(err); process.exit(1); }});
        """
        subprocess.check_call(["node", "-e", js_code])
        print(" -> [PASS] Node.js -> Java direct point-to-point RPC successful!")

        # ====== Test C: Python Server <- Java Client ======
        print("[Test 4] Python Server <- Java Client RPC...")
        py_uri = "tcp://127.0.0.1:20002"
        py_server = P2PServer(py_uri)

        @py_server.register("py.reverse")
        def py_reverse(s: str):
            return "".join(reversed(s))

        py_server.start(blocking=False)
        time.sleep(0.3)

        # Java code calling Python
        java_client_code = f"""
        import com.bridge.p2p.BridgeP2P;
        public class JavaToPyTest {{
            public static void main(String[] args) throws Exception {{
                BridgeP2P.Client client = new BridgeP2P.Client("{py_uri}");
                String res = client.call("py.reverse", "\\"HelloBridge\\"", 2000);
                System.out.println("Java received from Python: " + res);
                if (!"\\\"egdirBolleH\\\"".equals(res) && !"egdirBolleH".equals(res)) {{
                    System.exit(1);
                }}
                client.close();
            }}
        }}
        """
        with open("test/p2p_e2e/JavaToPyTest.java", "w") as f:
            f.write(java_client_code)

        subprocess.check_call([
            "javac", "-d", "build/p2p_classes",
            "-cp", "build/p2p_classes",
            "test/p2p_e2e/JavaToPyTest.java"
        ])
        subprocess.check_call([
            "java", "-cp", "build/p2p_classes", "JavaToPyTest"
        ])
        print(" -> [PASS] Java Client -> Python Server RPC successful!")
        py_server.stop()

        # ====== Test D: C Server <- Python Client ======
        print("[Test 5] C Native Server <- Python Client RPC...")
        c_uri = "tcp://127.0.0.1:20003"
        c_server_code = f"""
        #include "bridge_p2p.h"
        #include <stdio.h>
        #include <stdlib.h>
        #include <unistd.h>

        static char* c_uppercase(const char* method, const char* params_json, void* ud, int* err) {{
            (void)method; (void)ud; *err = 0;
            return strdup("\\"NATIVE_C_RESULT\\"");
        }}

        int main() {{
            P2PServer* s = p2p_server_create("{c_uri}");
            p2p_server_register_rpc(s, "c.upper", c_uppercase, NULL);
            p2p_server_start(s);
            return 0;
        }}
        """
        with open("test/p2p_e2e/c_server.c", "w") as f:
            f.write(c_server_code)

        subprocess.check_call([
            "gcc", "-O2", "-o", "build/c_server",
            "test/p2p_e2e/c_server.c",
            "-I", "p2p/c/include",
            "-L", "p2p/c", "-lbridge_p2p", "-lpthread"
        ])

        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = "p2p/c"
        c_proc = subprocess.Popen(["build/c_server"], env=env)
        time.sleep(0.3)

        py_c_client = P2PClient(c_uri)
        c_res = py_c_client.call("c.upper", {})
        print(f" -> Python received from C: {c_res}")
        assert c_res == "NATIVE_C_RESULT"
        py_c_client.close()
        c_proc.terminate()
        c_proc.wait()
        print(" -> [PASS] Python Client -> C Server RPC successful!")

        print("\n==================================================")
        print(" ALL CROSS-LANGUAGE P2P E2E TESTS PASSED (5/5)!")
        print("==================================================")

    finally:
        if java_proc.poll() is None:
            java_proc.terminate()
            java_proc.wait()

if __name__ == "__main__":
    test_cross_lang_e2e()
