"""Loopback-only TCP/UDP/SOCKS fixture for the disposable systemd VM."""
import os
import select
import socket
import struct
import threading


def exact(c, n):
    out = b""
    while len(out) < n:
        b = c.recv(n - len(out))
        if not b:
            raise EOFError
        out += b
    return out


def address(c):
    kind = exact(c, 1)[0]
    if kind == 1:
        host = socket.inet_ntop(socket.AF_INET, exact(c, 4))
    elif kind == 4:
        host = socket.inet_ntop(socket.AF_INET6, exact(c, 16))
    elif kind == 3:
        host = exact(c, exact(c, 1)[0]).decode()
    else:
        raise ValueError
    return host, struct.unpack("!H", exact(c, 2))[0]


def tcp_echo(c):
    with c:
        while b := c.recv(65536):
            c.sendall(b)


def udp_echo(host):
    with socket.socket(socket.AF_INET6 if ":" in host else socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind((host, 29001))
        while True:
            b, peer = s.recvfrom(65535)
            s.sendto(b, peer)


def socks(c):
    with c:
        try:
            ver, count = exact(c, 2)
            if ver != 5 or 0 not in exact(c, count):
                return
            c.sendall(b"\x05\x00")
            ver, cmd, reserved = exact(c, 3)
            target = address(c)
            if cmd == 1:
                with socket.create_connection(target, timeout=2) as upstream:
                    c.sendall(b"\x05\x00\x00\x01" + bytes(6))
                    while True:
                        ready, _, _ = select.select([c, upstream], [], [], 30)
                        for src in ready:
                            b = src.recv(65536)
                            if not b:
                                return
                            (upstream if src is c else c).sendall(b)
            elif cmd == 3:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    udp.bind(("127.0.0.1", 0))
                    host = b"slow.test" if os.path.exists("/tmp/ssh-udp-slow-relay") else b"localhost"
                    c.sendall(b"\x05\x00\x00\x03" + bytes([len(host)]) + host + struct.pack("!H", udp.getsockname()[1]))
                    while True:
                        ready, _, _ = select.select([c, udp], [], [], 30)
                        if c in ready and not c.recv(1):
                            return
                        if udp not in ready:
                            continue
                        b, peer = udp.recvfrom(65535)
                        # Datagram address decoder via a small in-memory reader.
                        class Reader:
                            def __init__(self, data): self.data, self.offset = data, 0
                            def recv(self, n):
                                v = self.data[self.offset:self.offset+n]; self.offset += len(v); return v
                        reader = Reader(b[3:])
                        target = address(reader)
                        header_len = 3 + reader.offset
                        af = socket.AF_INET6 if ":" in target[0] else socket.AF_INET
                        with socket.socket(af, socket.SOCK_DGRAM) as outbound:
                            outbound.settimeout(2)
                            outbound.sendto(b[header_len:], target)
                            reply, _ = outbound.recvfrom(65535)
                            udp.sendto(b[:header_len] + reply, peer)
        except (OSError, EOFError, ValueError):
            pass


def listen(port, handler):
    with socket.socket() as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", port))
        s.listen()
        while True:
            c, _ = s.accept()
            threading.Thread(target=handler, args=(c,), daemon=True).start()


for host in ("127.0.0.1", "::1"):
    threading.Thread(target=udp_echo, args=(host,), daemon=True).start()
threading.Thread(target=listen, args=(29001, tcp_echo), daemon=True).start()
listen(29080, socks)
