#	$OpenBSD$
#	Placed in the Public Domain.

tid="dynamic forwarding via ForwardHttpProxy"

if ! have_prog python3 ; then
	skip "python3 not found"
fi

PROXYPORT=`expr $PORT + 70`
TARGETPORT=`expr $PORT + 71`
SOCKSPORT=`expr $PORT + 72`
CTL=$OBJ/ctl-sock-forward-http-proxy
PROXY_SCRIPT=$OBJ/forward-http-proxy.py
PROXY_LOG=$OBJ/forward-http-proxy.log
TARGET_LOG=$OBJ/forward-http-target.log
OUT=$OBJ/forward-http-proxy.out

proxy_pid=
target_pid=

cleanup_local() {
	if [ -S "$CTL" ]; then
		${SSH} -q -F $OBJ/ssh_proxy -S "$CTL" -O exit somehost \
		    >/dev/null 2>&1
	fi
	[ -z "$proxy_pid" ] || kill "$proxy_pid" >/dev/null 2>&1
	[ -z "$target_pid" ] || kill "$target_pid" >/dev/null 2>&1
	rm -f "$CTL" "$PROXY_SCRIPT"
}

trap cleanup_local EXIT

cat > "$PROXY_SCRIPT" << 'EOF'
import socket
import threading
import select
import sys

host = "127.0.0.1"
port = int(sys.argv[1])
logfile = sys.argv[2]


def log(msg):
    with open(logfile, "a", encoding="utf-8") as f:
        f.write(msg + "\n")


def tunnel(a, b):
    sockets = [a, b]
    try:
        while True:
            r, _, _ = select.select(sockets, [], [], 5)
            if not r:
                continue
            for s in r:
                d = s.recv(8192)
                if not d:
                    return
                (b if s is a else a).sendall(d)
    finally:
        for s in sockets:
            try:
                s.close()
            except Exception:
                pass


def handle(c):
    try:
        data = b""
        while b"\r\n\r\n" not in data and len(data) < 8192:
            chunk = c.recv(1024)
            if not chunk:
                return
            data += chunk
        line = data.split(b"\r\n", 1)[0].decode("latin1", "replace")
        log(line)
        parts = line.split()
        if len(parts) < 3 or parts[0].upper() != "CONNECT":
            c.sendall(b"HTTP/1.1 405 Method Not Allowed\r\n\r\n")
            return
        hostport = parts[1]
        if hostport.startswith("["):
            h, p = hostport[1:].split("]:", 1)
        else:
            h, p = hostport.rsplit(":", 1)
        upstream = socket.create_connection((h, int(p)), timeout=5)
        c.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
        tunnel(c, upstream)
    except Exception:
        try:
            c.sendall(b"HTTP/1.1 502 Bad Gateway\r\n\r\n")
        except Exception:
            pass
    finally:
        try:
            c.close()
        except Exception:
            pass


ls = socket.socket()
ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind((host, port))
ls.listen(50)

while True:
    c, _ = ls.accept()
    threading.Thread(target=handle, args=(c,), daemon=True).start()
EOF

rm -f "$PROXY_LOG" "$TARGET_LOG" "$OUT" "$CTL"

python3 -u -m http.server "$TARGETPORT" --bind 127.0.0.1 \
    >"$TARGET_LOG" 2>&1 &
target_pid=$!

python3 -u "$PROXY_SCRIPT" "$PROXYPORT" "$PROXY_LOG" \
    > /dev/null 2>&1 &
proxy_pid=$!

cat >> $OBJ/sshd_proxy << EOF
ForwardHttpProxy 127.0.0.1:$PROXYPORT
EOF

${SSHD} -t -f $OBJ/sshd_proxy || fatal "sshd_proxy broken with ForwardHttpProxy"

${SSH} -q -N -f -F $OBJ/ssh_proxy \
    -M -S "$CTL" \
    -D 127.0.0.1:$SOCKSPORT \
    -oExitOnForwardFailure=yes somehost || fatal "failed to start -D forwarding"

printf 'GET / HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n' |
	$OBJ/netcat -x 127.0.0.1:$SOCKSPORT -X 5 127.0.0.1 $TARGETPORT > "$OUT" \
	|| fatal "failed HTTP fetch via dynamic SOCKS"

grep '^HTTP/1\.[01] 200' "$OUT" >/dev/null ||
	fatal "unexpected HTTP response through dynamic SOCKS"

grep "^CONNECT 127.0.0.1:$TARGETPORT HTTP/1.1" "$PROXY_LOG" >/dev/null ||
	fatal "missing CONNECT log from ForwardHttpProxy"
