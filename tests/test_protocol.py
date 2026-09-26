import asyncio
import json
from pathlib import Path
import sys
import contextlib
import socket
import tempfile
import types
import urllib.request
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from eeg_protocol import StreamParser, decode_metrics
from eeg_gateway import Gateway, batch_events, replay, run


def frame(payload):
    return b"\xaa\xaa" + bytes([len(payload)]) + bytes(payload) + bytes([255 - (sum(payload) & 255)])


def synthetic_capture(sample_count):
    data = bytearray()
    metrics = frame([2, 0, 0x83, 24] + [0] * 24 + [4, 50, 5, 60])
    for index in range(sample_count):
        value = (index % 201) - 100
        data.extend(frame([0x80, 2, (value >> 8) & 255, value & 255]))
        if (index + 1) % 32 == 0:
            data.extend(metrics)
    return bytes(data)


class ProtocolTests(unittest.TestCase):
    def test_chunk_boundaries_and_signed_raw(self):
        data = frame([0x80, 2, 0x80, 0]) + frame([0x80, 2, 0x7f, 0xff])
        for size in range(1, len(data) + 1):
            parser = StreamParser()
            events = []
            for i in range(0, len(data), size):
                events.extend(parser.feed(data[i:i + size]))
            self.assertEqual([e["samples"][0] for e in events], [-32768, 32767])
            self.assertEqual(parser.checksum_errors, 0)

    def test_nested_recovery(self):
        nested = frame([0x80, 2, 0, 7])
        bad = (b"\xaa\xaa\x20\x00" + nested).ljust(36, b"\x00")
        parser = StreamParser()
        self.assertEqual(parser.feed(bad)[0]["samples"], [7])
        self.assertGreater(parser.checksum_errors, 0)

    def test_metrics_order_and_bad_tail(self):
        data = frame([5, 20, 0x55, 2, 200, 4, 80, 2, 0])
        self.assertEqual(decode_metrics(data), {"meditation": 20, "attention": 80, "poor_signal": 0})
        with self.assertRaises(ValueError):
            decode_metrics(frame([4, 50, 0x83, 24, 0]))

    def test_synthetic_capture_and_trailing_partial(self):
        parser = StreamParser()
        events = parser.feed(synthetic_capture(64) + b"\xaa")
        self.assertEqual(parser.raw_count, 64)
        self.assertEqual(sum(e["type"] == "metrics" for e in events), 2)
        self.assertEqual(parser.checksum_errors, 0)
        self.assertEqual(len(parser.buffer), 1)

    def test_batches_preserve_metric_order(self):
        events = [{"type": "samples", "samples": [i], "sample_index": i} for i in range(70)]
        events.insert(12, {"type": "metrics", "poor_signal": 0})
        batches = list(batch_events(events, 248))
        self.assertEqual(batches[1]["type"], "metrics")
        self.assertEqual([v for b in batches if b["type"] == "samples" for v in b["samples"]], list(range(70)))
        self.assertTrue(all(len(b["samples"]) <= 32 for b in batches if b["type"] == "samples"))


class GatewayTests(unittest.IsolatedAsyncioTestCase):
    async def test_application_http_websocket_and_recording(self):
        from websockets.asyncio.client import connect
        with socket.socket() as http_probe, socket.socket() as ws_probe:
            http_probe.bind(("127.0.0.1", 0))
            ws_probe.bind(("127.0.0.1", 0))
            http_port = http_probe.getsockname()[1]
            ws_port = ws_probe.getsockname()[1]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic-capture.bin"
            path.write_bytes(synthetic_capture(256))
            recording = Path(directory) / "replay.jsonl"
            args = types.SimpleNamespace(replay=path, sample_rate=4096, http_port=http_port,
                                         ws_port=ws_port, record=recording)
            task = asyncio.create_task(run(args))
            try:
                for _ in range(100):
                    if task.done():
                        await task
                    try:
                        reader, writer = await asyncio.open_connection("127.0.0.1", http_port)
                    except OSError:
                        await asyncio.sleep(0.05)
                    else:
                        writer.close()
                        await writer.wait_closed()
                        break
                def fetch_page():
                    with urllib.request.urlopen(f"http://127.0.0.1:{http_port}/", timeout=5) as response:
                        return response.read()
                self.assertIn(b"eegChart", await asyncio.to_thread(fetch_page))
                async with connect(f"ws://127.0.0.1:{ws_port}/ws", proxy=None) as client:
                    while True:
                        event = json.loads(await asyncio.wait_for(client.recv(), 5))
                        if event.get("replay_complete"):
                            break
            finally:
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await task
            events = [json.loads(line) for line in recording.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(sum(len(e["samples"]) for e in events if e["type"] == "samples"), 256)
            self.assertTrue(any(e.get("source") == "replay" for e in events))

    async def test_full_historical_replay_over_websocket(self):
        from websockets.asyncio.server import serve
        from websockets.asyncio.client import connect
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic-capture.bin"
            path.write_bytes(synthetic_capture(512))
            gateway = Gateway("replay", 4096)
            async with serve(gateway.client, "127.0.0.1", 0) as server:
                port = server.sockets[0].getsockname()[1]
                async with connect(f"ws://127.0.0.1:{port}/ws", proxy=None) as socket:
                    await socket.recv()
                    task = asyncio.create_task(replay(gateway, path, 4096))
                    raw, metrics = 0, 0
                    while True:
                        event = json.loads(await asyncio.wait_for(socket.recv(), 5))
                        if event["type"] == "samples":
                            raw += len(event["samples"])
                        elif event["type"] == "metrics":
                            metrics += 1
                        elif event.get("replay_complete"):
                            break
                    await task
                    self.assertEqual((raw, metrics), (512, 16))

    async def test_real_websocket_and_idle_disconnect_cleanup(self):
        from websockets.asyncio.server import serve
        from websockets.asyncio.client import connect
        gateway = Gateway("replay", 248)
        async with serve(gateway.client, "127.0.0.1", 0) as server:
            port = server.sockets[0].getsockname()[1]
            async with connect(f"ws://127.0.0.1:{port}/ws", proxy=None) as socket:
                hello = json.loads(await socket.recv())
                self.assertEqual(hello["source"], "replay")
                gateway.publish({"type": "samples", "samples": [-2, 0, 3]})
                message = json.loads(await asyncio.wait_for(socket.recv(), 2))
                self.assertEqual(message["samples"], [-2, 0, 3])
                self.assertEqual(message["seq"], 0)
            for _ in range(20):
                if not gateway.clients:
                    break
                await asyncio.sleep(0.01)
            self.assertFalse(gateway.clients)

    async def test_slow_client_is_explicitly_disconnected(self):
        gateway = Gateway("serial")
        queue = asyncio.Queue(maxsize=2)
        gateway.clients.add(queue)
        for _ in range(3):
            gateway.publish({"type": "samples", "samples": [1]})
        self.assertNotIn(queue, gateway.clients)
        self.assertIsNone(queue.get_nowait())


if __name__ == "__main__":
    unittest.main()
