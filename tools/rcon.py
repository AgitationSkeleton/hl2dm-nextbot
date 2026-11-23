#!/usr/bin/env python3
"""Minimal Source RCON client.  rcon.py <host:port> <password-file> <command> [command ...]"""
import socket
import struct
import sys
import time


def packet(rid, kind, body):
    data = struct.pack("<ii", rid, kind) + body.encode() + b"\0\0"
    return struct.pack("<i", len(data)) + data


def recv_packet(sock):
    raw = b""
    while len(raw) < 4:
        chunk = sock.recv(4 - len(raw))
        if not chunk:
            raise ConnectionError("closed")
        raw += chunk
    size = struct.unpack("<i", raw)[0]
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("closed")
        data += chunk
    rid, kind = struct.unpack("<ii", data[:8])
    return rid, kind, data[8:-2].decode("utf-8", "replace")


def run(addr, password, commands, timeout=10.0):
    host, port = addr.split(":")
    s = socket.create_connection((host, int(port)), timeout=timeout)
    s.sendall(packet(1, 3, password))
    while True:
        rid, kind, _ = recv_packet(s)
        if kind == 2:
            if rid == -1:
                raise SystemExit("rcon auth failed")
            break
    out = []
    for i, cmd in enumerate(commands, start=10):
        s.sendall(packet(i, 2, cmd))
        s.sendall(packet(i + 1000, 0, ""))  # end marker trick
        text = ""
        while True:
            rid, kind, body = recv_packet(s)
            if rid == i + 1000:
                break
            text += body
        out.append("] %s\n%s" % (cmd, text))
        time.sleep(0.05)
    s.close()
    return "\n".join(out)


if __name__ == "__main__":
    pw = open(sys.argv[2]).read().strip()
    print(run(sys.argv[1], pw, sys.argv[3:]))
