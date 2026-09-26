import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import capture_serial


class ToolTests(unittest.TestCase):
    def test_partial_capture_is_saved_and_never_overwritten(self):
        class SerialError(Exception):
            pass

        class Port:
            in_waiting = 8
            reads = 0
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def write(self, data):
                self.command = data
            def reset_input_buffer(self):
                pass
            def read(self, length):
                self.reads += 1
                if self.reads == 1:
                    return b"\xaa\xaa\x04\x80\x02\x00\x00\x7d"
                raise SerialError("simulated unplug")

        port = Port()
        serial = types.SimpleNamespace(Serial=lambda **kwargs: port, SerialException=SerialError)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "capture.bin"
            args = ["capture_serial.py", "--port", "FAKE", "--out", str(out), "--settle", "0"]
            with patch.dict(sys.modules, {"serial": serial}), patch.object(sys, "argv", args), patch.object(capture_serial.time, "sleep"):
                with self.assertRaises(SystemExit):
                    capture_serial.main()
                metadata = json.loads(out.with_suffix(".bin.json").read_text())
                self.assertEqual(metadata["bytes"], 8)
                self.assertEqual(metadata["sha256"], hashlib.sha256(out.read_bytes()).hexdigest())
                self.assertIn("simulated unplug", metadata["error"])
                self.assertEqual(port.command, b"binary\n")
                with self.assertRaises(SystemExit):
                    capture_serial.main()
                self.assertEqual(out.stat().st_size, 8)

    def test_backup_verification_detects_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "sample.bin"
            source.write_bytes(b"original")
            digest = hashlib.sha256(b"original").hexdigest()
            (root / "SHA256SUMS.csv").write_text(f"path,size_bytes,sha256\nsample.bin,8,{digest}\n")
            command = [sys.executable, str(ROOT / "tools/verify_backup.py"), str(root)]
            result = subprocess.run(command, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            source.write_bytes(b"modified")
            result = subprocess.run(command, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"MISMATCH", result.stdout)

    def test_recursive_backup_rejected_before_copy(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/create_backup.py"),
            "--destination", str(ROOT), "--project-only"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"outside the project", result.stderr)


if __name__ == "__main__":
    unittest.main()
