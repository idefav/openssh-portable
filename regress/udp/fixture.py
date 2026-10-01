"""Isolated, network-none UDP/SSH interoperability fixture. No host mounts."""
import os
import select
import socket
import struct
import subprocess
import sys
import threading
import time

MODE = sys.argv[1] if len(sys.argv) > 1 else "direct"
SSH = "/opt/ssh-udp/sbin/sshd"

def exact(conn, n):
    out = b""
    while len(out) < n:
        data = conn.recv(n - len(out))
        if not data:
            raise EOFError()
        out += data
    return out

def echo(family, host, port):
    s = socket.socket(family, socket.SOCK_DGRAM)
    s.bind((host, port))
    while True:
        data, peer = s.recvfrom(65535)
        s.sendto(data, peer)

def association(conn):
    udp = None
    try:
        if MODE == "socks-hang":
            time.sleep(10)
            return
        ver, count = exact(conn, 2)
        methods = exact(conn, count)
        method = 2 if MODE == "socks-auth" else 0
        if method not in methods:
            conn.sendall(b"\x05\xff")
            return
        conn.sendall(bytes([5, method]))
        if method == 2:
            ver, n = exact(conn, 2)
            user = exact(conn, n)
            password = exact(conn, exact(conn, 1)[0])
            if user != b"fixture" or password != b"password":
                conn.sendall(b"\x01\x01")
                return
            conn.sendall(b"\x01\x00")
        if exact(conn, 10) != b"\x05\x03\x00\x01" + bytes(6):
            return
        if MODE == "socks-reject":
            conn.sendall(b"\x05\x07\x00\x01" + bytes(6))
            return
        family = socket.AF_INET6 if MODE == "socks-v6" else socket.AF_INET
        udp = socket.socket(family, socket.SOCK_DGRAM)
        udp.bind(("::1" if family == socket.AF_INET6 else "127.0.0.1", 0))
        # Exercise unspecified BND.ADDR handling and fragmented TCP replies.
        response = (b"\x05\x00\x00\x04" + bytes(16) if family == socket.AF_INET6 else b"\x05\x00\x00\x01" + bytes(4)) + struct.pack("!H", udp.getsockname()[1])
        for b in response:
            conn.sendall(bytes([b]))
        if MODE == "socks-close":
            time.sleep(0.2)
            return
        while True:
            ready, _, _ = select.select([conn, udp], [], [], 10)
            if conn in ready and not conn.recv(1):
                return
            if udp not in ready:
                continue
            data, peer = udp.recvfrom(65535)
            if len(data) < 4 or data[:3] != bytes(3):
                continue
            atyp = data[3]
            if atyp == 1:
                host, end, af = socket.inet_ntop(socket.AF_INET, data[4:8]), 8, socket.AF_INET
            elif atyp == 4:
                host, end, af = socket.inet_ntop(socket.AF_INET6, data[4:20]), 20, socket.AF_INET6
            elif atyp == 3:
                host, end, af = data[5:5+data[4]].decode(), 5+data[4], socket.AF_INET
            else:
                continue
            port = struct.unpack("!H", data[end:end+2])[0]
            with socket.socket(af, socket.SOCK_DGRAM) as target:
                target.settimeout(2)
                target.connect((host, port))
                target.send(data[end+2:])
                reply = target.recv(65535)
            udp.sendto(data[:end+2] + reply, peer)
    except (OSError, EOFError, ValueError):
        pass
    finally:
        if udp is not None:
            udp.close()
        conn.close()

def socks():
    listener = socket.socket(socket.AF_INET6 if MODE == "socks-v6" else socket.AF_INET)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("::1" if MODE == "socks-v6" else "127.0.0.1", 19080))
    listener.listen()
    while True:
        conn, _ = listener.accept()
        threading.Thread(target=association, args=(conn,), daemon=True).start()

for args in [(socket.AF_INET, "127.0.0.1", 19001), (socket.AF_INET6, "::1", 19001), (socket.AF_INET6, "::1", 19002)]:
    threading.Thread(target=echo, args=args, daemon=True).start()
threading.Thread(target=socks, daemon=True).start()

def start(name, port, extra):
    path = "/tmp/" + name + ".conf"
    with open(path, "w") as f:
        f.write("ListenAddress 127.0.0.1\nPort %d\nPermitRootLogin yes\nPasswordAuthentication yes\nStrictModes no\nLogLevel ERROR\n" % port)
        f.write(extra + "\n")
    subprocess.Popen([SSH, "-D", "-e", "-f", path], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    for _ in range(100):
        try:
            conn = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            conn.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("fixture sshd did not start")

extra = "AllowUdpForwarding yes\nAllowTcpForwarding no"
if MODE == "disabled":
    extra = "AllowUdpForwarding no"
elif MODE == "deny-all":
    extra += "\nDisableForwarding yes"
elif MODE == "acl":
    extra += "\nPermitOpen 127.0.0.1:19001"
elif MODE in ("key-deny", "key-acl"):
    restriction = "no-port-forwarding" if MODE == "key-deny" else 'permitopen="127.0.0.1:19001"'
    with open("/root/.ssh/authorized_keys", "w") as keyfile:
        keyfile.write(restriction + " " + sys.argv[2] + "\n")
elif MODE == "http":
    extra += "\nForwardProxy http://127.0.0.1:19080"
elif MODE.startswith("socks"):
    auth = "fixture:password@" if MODE == "socks-auth" else ""
    upstream = "localhost" if MODE == "socks-domain" else ("[::1]" if MODE == "socks-v6" else "127.0.0.1")
    extra += "\nForwardProxy socks5://" + auth + upstream + ":19080"
elif MODE == "match":
    extra = "AllowUdpForwarding no\nMatch User root\nAllowUdpForwarding yes"
start("final", 19022, extra)
port = 19022
if MODE == "relay":
    start("relay-b", 19024, "SSHRelayTarget 127.0.0.1:19022")
    start("relay-a", 19023, "SSHRelayTarget 127.0.0.1:19024")
    port = 19023
with socket.create_connection(("127.0.0.1", port)) as peer:
    while True:
        ready, _, _ = select.select([0, peer], [], [])
        if 0 in ready:
            data = os.read(0, 65536)
            if not data:
                break
            peer.sendall(data)
        if peer in ready:
            data = peer.recv(65536)
            if not data:
                break
            view = memoryview(data)
            while view:
                n = os.write(1, view)
                view = view[n:]
