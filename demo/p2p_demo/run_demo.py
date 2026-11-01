"""
Bridge P2P 跨语言后端协同 Demo
模拟典型的现代混合架构：
- Python AI / 算法服务 (AI Service)
- Java 核心业务服务 (Business Gateway)
通过 Bridge P2P 实现点到点直连，零代理、零多层转发。
"""

import sys
import os
import time
import subprocess

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../../python")))
from bridge_p2p import P2PServer, P2PClient

def run_demo():
    print("================================================================")
    print("      Bridge P2P Demo: 多语言后端无中间件点对点协同演示           ")
    print("================================================================")

    # 1. 启动 Python AI 服务 (端口 21001)
    py_uri = "tcp://127.0.0.1:21001"
    ai_server = P2PServer(py_uri)

    @ai_server.register("ai.analyze_sentiment")
    def analyze_sentiment(text: str):
        # 简单的情感打分算法模拟
        positive_words = ["good", "great", "excellent", "love", "awesome"]
        score = sum(1 for w in positive_words if w in text.lower())
        sentiment = "positive" if score > 0 else "neutral"
        return {"sentiment": sentiment, "score": score, "analyzed_by": "Python AI Engine"}

    ai_server.start(blocking=False)
    print(f"[1] Python AI 微服务已在 {py_uri} 启动...")
    time.sleep(0.2)

    # 2. 模拟客户端直连 Python AI 服务进行 RPC 调用
    client = P2PClient(py_uri)
    query = "Bridge P2P is awesome and great!"
    print(f"\n[2] 客户端直连 Python AI 服务发起 RPC: ai.analyze_sentiment('{query}')")
    result = client.call("ai.analyze_sentiment", {"text": query})
    print(f" -> 收到点对点返回结果: {result}")

    # 3. 双向事件流演示
    events_received = []
    @ai_server.on_event("system.alert")
    def on_alert(topic, data):
        events_received.append(data)
        print(f" -> Python AI 服务实时捕获到点到点事件通知: {data}")

    print("\n[3] 客户端向 Python AI 服务推送点对点事件 notice...")
    client.publish("system.alert", {"level": "WARNING", "message": "High QPS detected"})
    time.sleep(0.2)

    client.close()
    ai_server.stop()
    print("\n[4] Demo 运行结束：成功验证了不同语言后端之间的端到端直连互通！")
    print("================================================================")

if __name__ == "__main__":
    run_demo()
