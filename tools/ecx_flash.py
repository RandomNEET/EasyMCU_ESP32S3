#!/usr/bin/env python3
"""Upload ECX2 programs to EasyMCU; independent of the EasyCon GUI."""

import argparse
import pathlib
import os
import struct
import sys
import time
import zlib

MAX_IMAGE = 65536
ACK, BUSY = b"\xff", b"\xfe"


def frame(op, payload=b""):
    if len(payload) > 260:
        raise ValueError("Payload exceeds 260 bytes")
    body = struct.pack("<BH", op, len(payload)) + payload
    return b"\xa5\x90" + body + struct.pack("<I", zlib.crc32(body))


def validate_image(data):
    if not 36 <= len(data) <= MAX_IMAGE:
        raise ValueError("ECX image must be 36..65536 bytes")
    if data[:6] != b"EXC2\x02\x00":
        raise ValueError(
            "Expected ECX2 format version 2; legacy bytecode is not accepted"
        )
    flags, features = (
        struct.unpack_from("<H", data, 6)[0],
        struct.unpack_from("<H", data, 10)[0],
    )
    if flags & 4 or features & ~8:
        raise ValueError(
            "Image needs PC capabilities (image/capture/FFI); use a pad-only script"
        )


class Client:
    def __init__(self, port):
        self.port = port

    def probe(self, timeout=5):
        """Use the original EasyCon handshake before attempting any ECX write."""
        previous_timeout = self.port.timeout
        self.port.timeout = 0.2
        deadline = time.monotonic() + timeout
        try:
            self.port.reset_input_buffer()
            while time.monotonic() < deadline:
                self.port.write(b"\xa5\xa5\x81")  # READY, READY, HELLO
                self.port.flush()
                while time.monotonic() < deadline:
                    reply = self.port.read(1)
                    if not reply:
                        break
                    if reply == b"\x80":
                        self.port.timeout = previous_timeout
                        self.port.write(b"\xa5\x85")
                        self.port.flush()
                        version = self.port.read(1)
                        if not version:
                            raise RuntimeError(
                                "HELLO succeeded, but firmware version query timed out"
                            )
                        return version[0]
                time.sleep(0.05)
        finally:
            self.port.timeout = previous_timeout
        raise RuntimeError(
            "No EasyCon HELLO reply: application firmware is not reachable. "
            "Release BOOT and press RST, close EasyCon/serial monitors, "
            "and check the COM port (UART0 GPIO43/44, 115200). "
            "For native USB CDC try --dtr on"
        )

    def command(self, op, payload=b""):
        self.port.reset_input_buffer()
        self.port.write(frame(op, payload))
        self.port.flush()
        reply = self.port.read(1)
        if reply == BUSY:
            raise RuntimeError("Board is busy; stop the running script first")
        if reply != ACK:
            operation = {
                0: "BEGIN",
                1: "DATA",
                2: "COMMIT",
                3: "START",
                4: "STOP",
                5: "CLEAR",
            }.get(op, str(op))
            raise RuntimeError(
                f"{operation}: board rejected the request (reply {reply.hex()}); "
                "confirm the board runs the latest feat/ecx-flash firmware"
                if reply
                else f"{operation}: application handshake succeeded, but ECX request timed out; "
                "check UART1 logs or try --timeout 15"
            )

    def upload(self, data, auto=False):
        validate_image(data)
        self.command(0, struct.pack("<IIB", len(data), zlib.crc32(data), int(auto)))
        for offset in range(0, len(data), 256):
            self.command(1, struct.pack("<I", offset) + data[offset : offset + 256])
        self.command(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", "-p", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument(
        "--dtr",
        choices=("auto", "on", "off"),
        default="auto",
        help="CDC needs DTR on; auto enables it for ttyACM ports",
    )
    parser.add_argument(
        "--boot-wait",
        type=float,
        default=1.2,
        help="seconds to wait after opening the port (default: 1.2)",
    )
    sub = parser.add_subparsers(dest="command", required=True)
    upload = sub.add_parser("upload")
    upload.add_argument("image", type=pathlib.Path)
    upload.add_argument(
        "--auto-run", action="store_true", help="run on subsequent boots"
    )
    upload.add_argument(
        "--start", action="store_true", help="start immediately after upload"
    )
    for command in ("probe", "start", "stop", "clear"):
        sub.add_parser(command)
    args = parser.parse_args()
    try:
        import serial

        # Keep RTS deasserted; assert DTR for native CDC so tud_cdc_connected()
        # permits firmware replies. USB-UART bridges keep DTR off by default.
        with serial.Serial(
            port=None,
            baudrate=args.baud,
            timeout=args.timeout,
            write_timeout=args.timeout,
            exclusive=True,
        ) as port:
            port.rts = False
            port.dtr = args.dtr == "on" or (
                args.dtr == "auto" and "ttyACM" in os.path.realpath(args.port)
            )
            port.port = args.port
            port.open()
            time.sleep(args.boot_wait)
            client = Client(port)
            version = client.probe(args.timeout)
            print(
                f"EasyCon application connected, firmware 0x{version:02X}, DTR={port.dtr}"
            )
            if args.command == "probe":
                return 0
            if args.command == "upload":
                data = args.image.read_bytes()
                client.upload(data, args.auto_run)
                print(f"Saved {len(data)} bytes; boot auto-run: {args.auto_run}")
                if args.start:
                    client.command(3)
            else:
                client.command({"start": 3, "stop": 4, "clear": 5}[args.command])
            print("OK (accepted by firmware; inspect OLED for execution result)")
    except (ImportError, OSError, ValueError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
