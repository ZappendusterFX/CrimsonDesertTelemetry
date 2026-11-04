"""Synthetic loopback checks; never connects to the game or changes devices."""
import base64
import gzip
import hashlib
import json
from pathlib import Path
import socketserver
import struct
import subprocess
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "artifacts/tools/visibility-recorder/VisibilityRecorder.exe"
EVIDENCE = ROOT / "artifacts/tools" / ("recorder-check-" + uuid.uuid4().hex)
EVIDENCE.mkdir(parents=True)
RGB = {"x": 3.0, "y": 0.25, "z": 0.125}
SOURCE = {"sampleIndex": 0, "position": {"x": 1, "y": 2, "z": 3}, "colorLinear": RGB,
          "kind": "point", "sourceVisibility": {"status": "clear", "method": "physics-ray-fan"}}
RAW = {"schemaVersion": "1.6", "sequence": 1, "capturedAt": "2026-11-04T12:00:00Z",
       "game": {"state": "playing"}, "player": {}, "camera": {},
       "lights": {"authored": {"mustNotBeRetained": True},
                  "upstream": {"status": "available", "captureSequence": 1, "sources": [SOURCE]},
                  "rendered": {"status": "available", "captureSequence": 1, "sources": [SOURCE]}}}


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        request = b""
        self.request.settimeout(3)
        try:
            while b"\r\n\r\n" not in request:
                chunk = self.request.recv(4096)
                if not chunk or len(request) > 32768:
                    return
                request += chunk
            lines = request.decode("ascii").split("\r\n")
            path = lines[0].split()[1]
            headers = dict(line.split(": ", 1) for line in lines[1:] if ": " in line)
            key = next(value for name, value in headers.items() if name.lower() == "sec-websocket-key")
            accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
            self.request.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
            ambient = path == "/v1/ambient/stream"
            if ambient and self.server.fail_ambient:
                return
            for index in range(50):
                sample = {"status": "available" if index % 2 else "unavailable", "captureSequence": index} if ambient else dict(RAW, sequence=index)
                payload = json.dumps(sample).encode()
                prefix = bytes([0x81, len(payload)]) if len(payload) < 126 else bytes([0x81, 126]) + struct.pack(">H", len(payload))
                self.request.sendall(prefix + payload)
                time.sleep(0.05)
        except (ConnectionError, OSError):
            pass


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True


def run(name, fail_ambient=False):
    with Server(("127.0.0.1", 0), Handler) as server:
        server.fail_ambient = fail_ambient
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        thread.start()
        output = EVIDENCE / name
        try:
            result = subprocess.run([str(EXE), "1", "--lighting", "--port", str(server.server_address[1]),
                                     "--output", str(output)], capture_output=True, text=True, timeout=8)
        finally:
            server.shutdown()
            thread.join(timeout=2)
    return result, output, json.loads((output / "summary.json").read_text())


result, output, summary = run("complete")
assert result.returncode == 0 and summary["result"] == "duration-complete", summary
assert summary["frames"] >= 5 and summary["ambientFrames"] >= 5, summary
assert summary["elapsedMs"] < 5000, summary
with gzip.open(output / "visibility.jsonl.gz", "rt") as trace:
    frames = [json.loads(line) for line in trace]
assert frames[0]["lights"]["upstream"]["sources"][0]["colorLinear"] == RGB
assert "authored" not in frames[0]["lights"]
with gzip.open(output / "ambient.jsonl.gz", "rt") as trace:
    ambient = [json.loads(line) for line in trace]
assert {frame["sample"]["status"] for frame in ambient} == {"available", "unavailable"}
old_summary = (output / "summary.json").read_bytes()
collision = subprocess.run([str(EXE), "1", "--output", str(output)], capture_output=True, timeout=5)
assert collision.returncode != 0 and (output / "summary.json").read_bytes() == old_summary

result, failed_output, failed_summary = run("ambient-disconnected", fail_ambient=True)
assert result.returncode != 0 and failed_summary["result"] == "failed", failed_summary
assert (failed_output / "visibility.jsonl.gz").exists()

fixture = EVIDENCE / "replay.jsonl"
fixture.write_text(json.dumps(RAW) + "\n" + json.dumps(dict(RAW, sequence=2)) + "\n")
replay_output = EVIDENCE / "replay"
replay = subprocess.run([str(EXE), "--replay", str(fixture), "--lighting", "--output", str(replay_output)],
                        capture_output=True, timeout=5)
assert replay.returncode == 0
assert json.loads((replay_output / "summary.json").read_text())["frames"] == 2
assert not (replay_output / "ambient.jsonl.gz").exists()
print(json.dumps({"result": "passed", "checks": ["concurrent RGB/Ambient streams", "bounded duration",
      "available/unavailable retained", "existing evidence protected", "disconnect preserves partial failure",
      "offline replay"], "directory": str(EVIDENCE)}))
