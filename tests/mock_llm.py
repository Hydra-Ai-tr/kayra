"""A tiny OpenAI-compatible chat server for testing Kayra's brain path without a
real model: streams text (SSE), makes a tool call, hides <think> blocks.

    python tests/mock_llm.py 8765
    kayra-cli brain   (with base_url http://127.0.0.1:8765/v1)
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def chunks(text, n=6):
    for i in range(0, len(text), n):
        yield text[i:i + n]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.endswith("/models"):
            body = json.dumps({"data": [{"id": "mock-1"}]}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def do_POST(self):
        req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        msgs = req.get("messages", [])
        last_user = next((m["content"] for m in reversed(msgs) if m["role"] == "user"), "")
        tool_msgs = [m for m in msgs if m["role"] == "tool"]
        tools = {t["function"]["name"] for t in req.get("tools", [])}
        stream = req.get("stream", False)

        tool_call = None
        if tool_msgs:
            data = json.loads(tool_msgs[-1]["content"] or "{}")
            text = "Araç sonucu geldi: " + (data.get("say") or json.dumps(data, ensure_ascii=False))[:160]
        elif "hava" in last_user.lower() and "get_weather" in tools:
            tool_call = {"id": "call_1", "type": "function",
                         "function": {"name": "get_weather", "arguments": json.dumps({"city": "Ankara"})}}
            text = ""
        else:
            text = "<think>kullanıcı selam verdi</think>Merhaba efendim! Ben sahte bir beyinim. Size nasıl yardımcı olabilirim?"

        if not stream:
            msg = {"role": "assistant", "content": text}
            if tool_call:
                msg["tool_calls"] = [tool_call]
            body = json.dumps({"choices": [{"index": 0, "message": msg,
                                            "finish_reason": "tool_calls" if tool_call else "stop"}]}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()

        def send(obj):
            self.wfile.write(b"data: " + json.dumps(obj).encode() + b"\n\n")
            self.wfile.flush()

        if tool_call:
            args = tool_call["function"]["arguments"]
            send({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "id": "call_1", "type": "function",
                                                                       "function": {"name": "get_weather", "arguments": ""}}]}}]})
            for part in chunks(args, 5):
                send({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "function": {"arguments": part}}]}}]})
            send({"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]})
        else:
            for part in chunks(text):
                send({"choices": [{"index": 0, "delta": {"content": part}}]})
                time.sleep(0.01)
            send({"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]})
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
