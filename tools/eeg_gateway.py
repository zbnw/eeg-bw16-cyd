"""Local CYD serial / historical BIN -> WebSocket dashboard gateway.

Run with --port COMx for live JSON firmware, --format binary for legacy firmware,
or --replay FILE --sample-rate 248 for clearly labelled historical replay.
All listeners bind to 127.0.0.1. No serial commands are accepted from browsers.
"""

import argparse
import asyncio
import contextlib
import functools
import http.server
import json
import math
from pathlib import Path
import threading
import time
import uuid

from eeg_protocol import StreamParser


class Gateway:
    def __init__(self, source, sample_rate=None):
        self.clients = set()
        self.seq = 0
        self.record = None
        self.hello = {"v": 1, "type": "hello", "device_id": "CYD", "firmware": "unknown",
                      "source": source, "session_id": uuid.uuid4().hex}
        if sample_rate:
            self.hello["sample_rate_hz"] = sample_rate

    def publish(self, event):
        if event.get("type") == "hello":
            # Preserve replay/live provenance assigned by the selected input mode.
            self.hello.update({k: v for k, v in event.items() if k not in ("source", "seq")})
            event = dict(self.hello)
            self.seq = 0
        else:
            event = {**event, "v": 1, "seq": self.seq, "host_received_us": time.time_ns() // 1000}
            self.seq += 1
        text = json.dumps(event, separators=(",", ":"), allow_nan=False)
        if self.record:
            self.record.write(text + "\n")
            self.record.flush()
        for queue in tuple(self.clients):
            if queue.full():
                # Closing forces an explicit session refresh rather than silent backlog loss.
                while not queue.empty():
                    queue.get_nowait()
                queue.put_nowait(None)
                self.clients.discard(queue)
            else:
                queue.put_nowait(text)

    async def client(self, socket):
        if socket.request.path != "/ws":
            await socket.close(code=1008, reason="Use /ws")
            return
        queue = asyncio.Queue(maxsize=64)
        self.clients.add(queue)
        async def send_messages():
            while True:
                message = await queue.get()
                if message is None:
                    await socket.close(code=1013, reason="Client too slow; reconnect")
                    return
                await socket.send(message)
        tasks = []
        try:
            await socket.send(json.dumps(self.hello))
            # One writer and one lifetime waiter, rather than two new tasks per message.
            tasks = [asyncio.create_task(send_messages()), asyncio.create_task(socket.wait_closed())]
            await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        finally:
            self.clients.discard(queue)
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)


def batch_events(events, rate=None):
    batch = []
    first_index = 0
    for event in events:
        if event["type"] == "samples":
            if not batch:
                first_index = event["sample_index"]
            batch.extend(event["samples"])
            if len(batch) < 32:
                continue
        if batch:
            message = {"type": "samples", "samples": batch, "sample_index": first_index}
            if rate:
                message["sample_rate_hz"] = rate
            yield message
            batch = []
        if event["type"] != "samples":
            yield event
    if batch:
        message = {"type": "samples", "samples": batch, "sample_index": first_index}
        if rate:
            message["sample_rate_hz"] = rate
        yield message


async def replay(gateway, path, rate):
    # Start on first viewer so the beginning isn't lost while opening the browser.
    while not gateway.clients:
        await asyncio.sleep(0.1)
    parser = StreamParser()
    gateway.publish({"type": "hello", "device_id": path.name, "firmware": "historical-bin",
                     "session_id": uuid.uuid4().hex, "sample_rate_hz": rate})
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024), b""):
            for event in batch_events(parser.feed(chunk), rate):
                gateway.publish(event)
                if event["type"] == "samples":
                    # Do not catch up with an unbounded burst after OS scheduling delays.
                    await asyncio.sleep(len(event["samples"]) / rate)
    gateway.publish({"type": "status", "connected": False, "replay_complete": True,
                     "checksum_errors": parser.checksum_errors, "trailing_bytes": len(parser.buffer)})


async def serial_source(gateway, args):
    import serial
    while True:
        port = None
        try:
            port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            # Leave reset/boot time before requesting the output format.
            await asyncio.sleep(1.5)
            port.reset_input_buffer()
            port.write((args.format + "\n").encode("ascii"))
            gateway.publish({"type": "hello", "device_id": args.port, "firmware": "unknown",
                             "session_id": uuid.uuid4().hex})
            parser = StreamParser()
            pending = bytearray()
            last_hello_request = time.monotonic()
            last_json_at = time.monotonic()
            last_binary_status = time.monotonic()
            hello_received = args.format == "binary"
            while True:
                data = await asyncio.to_thread(port.read, min(port.in_waiting or 1, 4096))
                if args.format == "binary":
                    for event in batch_events(parser.feed(data), args.sample_rate):
                        gateway.publish(event)
                    if time.monotonic() - last_binary_status > 1:
                        last_binary_status = time.monotonic()
                        gateway.publish({"type": "status", "connected": True,
                                         "checksum_errors": parser.checksum_errors,
                                         "uart_skipped_bytes": parser.skipped_bytes})
                else:
                    pending.extend(data)
                    while b"\n" in pending:
                        line, _, pending = pending.partition(b"\n")
                        if len(line) > 8192:
                            continue
                        try:
                            event = json.loads(line)
                            if not isinstance(event, dict) or event.get("v") != 1 or event.get("type") not in ("hello", "samples", "metrics", "status"):
                                continue
                            if event["type"] == "hello":
                                hello_received = True
                            gateway.publish(event)
                            last_json_at = time.monotonic()
                        except (ValueError, UnicodeError):
                            continue
                    if len(pending) > 8192:
                        pending.clear()
                    # A CYD reset can leave the COM port open while restoring binary mode.
                    if time.monotonic() - last_json_at > 6:
                        hello_received = False
                    if not hello_received and time.monotonic() - last_hello_request > 2:
                        port.write(b"json\n")
                        last_hello_request = time.monotonic()
        except (serial.SerialException, OSError) as exc:
            print(f"Serial unavailable: {exc}; retrying in 2 s", flush=True)
            gateway.publish({"type": "status", "connected": False})
        finally:
            if port is not None:
                if port.is_open:
                    with contextlib.suppress(serial.SerialException, OSError):
                        port.write(b"binary\n")
                port.close()
        await asyncio.sleep(2)


async def run(args):
    from websockets.asyncio.server import serve
    gateway = Gateway("replay" if args.replay else "serial", args.sample_rate)
    web = Path(__file__).resolve().parents[1] / "web"
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(web))
    http_server = http.server.ThreadingHTTPServer(("127.0.0.1", args.http_port), handler)
    http_server.daemon_threads = True
    threading.Thread(target=http_server.serve_forever, daemon=True).start()
    try:
        if args.record:
            gateway.record = args.record.open("x", encoding="utf-8")
        origins = [f"http://127.0.0.1:{args.http_port}", f"http://localhost:{args.http_port}", None]
        async with serve(gateway.client, "127.0.0.1", args.ws_port, origins=origins, max_size=8192):
            print(f"Open http://127.0.0.1:{args.http_port}/?ws=ws://127.0.0.1:{args.ws_port}/ws", flush=True)
            if args.replay:
                await replay(gateway, args.replay, args.sample_rate)
                print("Replay complete; Ctrl+C to stop or restart to replay again.", flush=True)
                await asyncio.Future()
            else:
                await serial_source(gateway, args)
    finally:
        if gateway.record:
            gateway.record.close()
        await asyncio.to_thread(http_server.shutdown)
        http_server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port")
    source.add_argument("--replay", type=Path)
    parser.add_argument("--format", choices=("json", "binary"), default="json")
    parser.add_argument("--sample-rate", type=float, help="Explicit approximate rate for BIN input")
    parser.add_argument("--record", type=Path, help="Save all outgoing messages as new JSONL, including diagnostics")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--ws-port", type=int, default=8765)
    args = parser.parse_args()
    if args.record:
        if args.record.exists():
            parser.error("record file already exists")
        args.record.parent.mkdir(parents=True, exist_ok=True)
    if args.replay and not args.replay.is_file():
        parser.error("replay file does not exist")
    if args.replay and args.sample_rate is None:
        parser.error("--replay requires --sample-rate (the BIN has no timestamps)")
    if args.sample_rate is not None and (not math.isfinite(args.sample_rate) or not 1 <= args.sample_rate <= 4096):
        parser.error("sample rate must be 1..4096")
    if any(not 1 <= port <= 65535 for port in (args.http_port, args.ws_port)) or args.http_port == args.ws_port:
        parser.error("choose two different valid listener ports")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
