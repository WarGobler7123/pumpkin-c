#!/usr/bin/env python3
"""End-to-end check for pumpkin-c's status/ping slice.

Starts the server on a scratch port, performs a real Java Edition
status handshake (handshake -> status request -> status response ->
ping -> pong), and asserts the response JSON parses and the ping
payload is echoed back exactly. Also checks the login path returns a
disconnect instead of hanging.
"""

import json
import socket
import struct
import subprocess
import sys
import time


def varint(n):
    out = bytearray()
    n &= 0xFFFFFFFF
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def packet(payload):
    return varint(len(payload)) + payload


def read_varint(f):
    value = 0
    for i in range(5):
        b = f.read(1)
        assert b, "connection closed while reading varint"
        byte = b[0]
        value |= (byte & 0x7F) << (7 * i)
        if not (byte & 0x80):
            return value
    raise AssertionError("varint too long")


def read_packet(f):
    length = read_varint(f)
    data = f.read(length)
    assert len(data) == length, "short packet"
    return data


def handshake(f, next_state):
    addr = b"localhost"
    payload = varint(0) + varint(767) + varint(len(addr)) + addr
    payload += struct.pack(">H", 25565) + varint(next_state)
    f.write(packet(payload))


def main():
    port = 25566
    proc = subprocess.Popen(
        ["./pumpkin-c", str(port), "Pumpkin-C test"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        # Wait for the listen line, up to 5 seconds.
        for _ in range(50):
            time.sleep(0.1)
            try:
                s = socket.create_connection(("127.0.0.1", port), timeout=1)
                s.close()
                break
            except OSError:
                continue
        else:
            raise AssertionError("server did not start listening")

        # Status path.
        with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
            f = s.makefile("rwb")
            handshake(f, 1)
            f.write(packet(varint(0)))
            f.flush()
            data = read_packet(f)
            assert data[0] == 0, f"expected status response id 0, got {data[0]}"
            # Payload after id: varint string length, then JSON.
            i = 1
            shift = 0
            slen = 0
            while True:
                b = data[i]
                i += 1
                slen |= (b & 0x7F) << shift
                shift += 7
                if not (b & 0x80):
                    break
            status = json.loads(data[i:i + slen])
            print("status:", json.dumps(status))
            assert status["description"]["text"] == "Pumpkin-C test"
            assert status["players"]["max"] == 20

            ping_value = 1234567890123456789
            f.write(packet(varint(1) + struct.pack(">q", ping_value)))
            f.flush()
            pong = read_packet(f)
            assert pong[0] == 1, f"expected pong id 1, got {pong[0]}"
            echoed = struct.unpack(">q", pong[1:9])[0]
            assert echoed == ping_value, f"pong mismatch: {echoed}"
            print("pong echoed correctly:", echoed)

        # Login path: must get a disconnect, not a hang.
        with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
            f = s.makefile("rwb")
            handshake(f, 2)
            f.flush()
            data = read_packet(f)
            assert data[0] == 0, f"expected disconnect id 0, got {data[0]}"
            print("login disconnect received, length:", len(data))

        print("PASS")
    finally:
        proc.terminate()
        proc.wait(timeout=5)


if __name__ == "__main__":
    sys.exit(main())
