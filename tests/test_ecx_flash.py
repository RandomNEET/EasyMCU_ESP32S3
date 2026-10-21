"""Run the real Python uploader against the C firmware parser over pipes."""

import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import unittest
from unittest.mock import patch
import tempfile
import zlib

spec = importlib.util.spec_from_file_location(
    "ecx_flash", Path(__file__).parents[1] / "tools/ecx_flash.py"
)
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)


class PipePort:
    def __init__(self):
        self.timeout = 5
        self.process = subprocess.Popen(
            [os.environ.get("ECX_TEST_BINARY", "/tmp/easymcu-test-ecx"), "--transport"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )

    def reset_input_buffer(self):
        pass

    def write(self, data):
        return self.process.stdin.write(data)

    def flush(self):
        self.process.stdin.flush()

    def read(self, size):
        return self.process.stdout.read(size)

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        self.process.stdout.close()
        assert self.process.returncode == 0


def image(words):
    # Numeric magic is exactly EcxWriter.Magic (the upstream comment swaps C/X).
    header = struct.pack("<IHHBBH6I", 0x32435845, 2, 0, 2, 4, 0, 0, 0, 0, 0, 1, 0)
    function = struct.pack("<BBBII", 0, 2, 0, 0, len(words))
    return (
        header
        + function
        + struct.pack(f"<{len(words)}I", *words)
        + struct.pack("<I", 0)
    )


class UploadTests(unittest.TestCase):
    def test_upload_start_stop_clear(self):
        port = PipePort()
        try:
            client = flash.Client(port)
            self.assertEqual(client.probe(), 0x46)
            self.assertEqual(port.timeout, 5)
            client.upload(
                image([0] * 130 + [1]), auto=True
            )  # multiple 256-byte packets; Nop/Halt
            client.command(3)
            client.command(4)
            client.command(5)
            with self.assertRaisesRegex(RuntimeError, "rejected"):
                client.command(3)  # cleared program cannot start
        finally:
            port.close()

    def test_boot_from_stored_blob(self):
        program = image([1])
        blob = (
            b"ECF2"
            + struct.pack("<IIB3x", len(program), zlib.crc32(program), 1)
            + program
        )
        with tempfile.NamedTemporaryFile() as file:
            file.write(blob)
            file.flush()
            subprocess.run(
                [
                    os.environ.get("ECX_TEST_BINARY", "/tmp/easymcu-test-ecx"),
                    "--boot",
                    file.name,
                ],
                check=True,
                timeout=5,
            )

    def test_probe_no_reply(self):
        class SilentPort:
            timeout = 5
            writes = []

            def reset_input_buffer(self):
                pass

            def write(self, data):
                self.writes.append(data)

            def flush(self):
                pass

            def read(self, count):
                return b""

        port = SilentPort()
        with patch.object(flash.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "No EasyCon HELLO reply"):
                flash.Client(port).probe(timeout=0.002)
        self.assertEqual(port.timeout, 5)
        self.assertTrue(port.writes)
        self.assertTrue(all(data == b"\xa5\xa5\x81" for data in port.writes))

    def test_frame_crc(self):
        data = flash.frame(1, b"\x00\x80\xff\xa5")
        self.assertEqual(data[:2], b"\xa5\x90")
        self.assertEqual(struct.unpack("<I", data[-4:])[0], zlib.crc32(data[2:-4]))

    def test_print_feature_flag(self):
        program = bytearray(image([1]))
        program[10] = 8
        flash.validate_image(program)
        port = PipePort()
        try:
            client = flash.Client(port)
            client.upload(program)
            client.command(3)
            client.command(5)
        finally:
            port.close()

    def test_reject_image(self):
        with self.assertRaises(ValueError):
            flash.validate_image(b"legacy")
        program = bytearray(image([1]))
        program[10] = 2  # requires capture
        with self.assertRaisesRegex(ValueError, "PC capabilities"):
            flash.validate_image(program)


if __name__ == "__main__":
    unittest.main()
