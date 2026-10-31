# Deploy the UDP extension as a separate SSH service

Use a dedicated TCP listener (example: 10026), installation prefix, privilege-separation directory and systemd unit. Keep the system SSH daemon, its management listener and its binaries in place. The extension transports UDP inside authenticated SSH/TCP; no UDP 10026 firewall rule is needed.

On a Linux server with PAM, OpenSSL, zlib and build tools installed:

```sh
autoreconf
./configure --prefix=/opt/openssh-udp --sysconfdir=/etc/ssh \
  --with-privsep-path=/var/empty/sshd-udp --with-pam
make -j2 sshd sshd-session sshd-auth
sudo install -d -m 0755 /opt/openssh-udp/sbin /opt/openssh-udp/libexec /var/empty/sshd-udp
sudo install -m 0755 sshd /opt/openssh-udp/sbin/
sudo install -m 0755 sshd-session sshd-auth /opt/openssh-udp/libexec/
sudo /opt/openssh-udp/sbin/sshd -t -f /etc/ssh/sshd_config -p 10026 -oAllowUdpForwarding=yes
sudo install -m 0644 contrib/sshd-udp.service /etc/systemd/system/
sudo systemd-analyze verify /etc/systemd/system/sshd-udp.service
sudo systemctl daemon-reload
sudo systemctl enable --now sshd-udp.service
```

Inspect the reused configuration before starting: explicit ports on ListenAddress must not bind the management port. Preserve the existing host keys, authentication policy, PAM configuration and authorized_keys restrictions. The stable `/var/empty/sshd-udp` directory must be owned by root and not writable by other users. Do not share `/run/sshd`: stopping the system service can remove its RuntimeDirectory and break the separate daemon.

Check the original listener and the new listener independently:

```sh
sudo systemctl status sshd-udp.service
sudo ss -lntp
sudo journalctl -u sshd-udp.service -n 50 --no-pager
```

Allow inbound **TCP 10026** in both the host firewall and cloud security group. A healthy local listener alone does not prove public reachability. Confirm the original management SSH still accepts fresh connections.

Compatible clients must verify the expected host key and authenticate, then query `udp-forwarding-v1@idefav.github.io`. An accepted response has one status byte: 0 enabled, 1 disabled by policy, 2 unsupported upstream; an unrecognized request is not extension support. Then verify actual STUN, DNS and HTTP3 traffic, not just UDP ASSOCIATE or the capability response.

`AllowUdpForwarding` defaults to no. DisableForwarding, per-key restrictions and PermitOpen remain effective. HTTP/SOCKS4 ForwardProxy cannot provide UDP; a SOCKS5 upstream must support UDP ASSOCIATE. Failures never fall back to direct UDP. The framing and limits are documented in README.md.

Transparent SSHRelayTarget relays need no UDP extension: change only their target TCP port to the new final listener and reload the relay after syntax validation. The client still connects to the relay's existing listener and verifies the final server's host key.

Rollback this independent listener with `systemctl disable --now sshd-udp.service`. Retain the original management service and its configuration throughout. Never disable or replace the original service as part of this procedure.

## tun2proxy gateway and precise ForwardProxy exceptions

The paired sing-box/v2rayN client also supports the official tun2proxy
`udpgw-server` over ordinary `direct-tcpip`. A stock SSH server can use this
mode without the private UDP extension. The gateway's UDP uses its host's own
egress, not this daemon's SOCKS5 upstream.

When ForwardProxy is configured, allow an exact local gateway endpoint:

```text
Match User alice
    ForwardProxyBypass 127.0.0.1:7300
```

The default is `none`. Only numeric loopback host/port pairs are accepted
(IPv6 in brackets); no wildcard or hostname is allowed. TCP forwarding,
DisableForwarding, PermitOpen and authenticated-key restrictions still apply.
Use `sshd -t`, reload the identified service, then verify with a **new** SSH
connection. Existing sessions retain their original effective configuration.

The authenticated `forward-proxy-info-v1@idefav.github.io` policy request
reports effective endpoints and restriction reasons without upstream URLs or
credentials; see PROTOCOL. The original one-byte UDP capability reply is
unchanged. UDP DNS resolution now runs in bounded children (four per SSH
connection); queueing, DNS and upstream negotiation share the three-second
setup deadline. Cancellation closes the pipe and kills/reaps the resolver.

v2rayN's optional Linux/systemd deployment helper only adds the current
account's gateway exception. It does not replace sshd. Unknown wrappers or
ambiguous `-o` overrides must be resolved by the administrator first; put the
intended policy in the identified configuration file. The helper arms a
persistent rollback timer before changes and verifies a new connection before
committing. Do not manually remove a pending transaction's timer or backups.

## Isolated interoperability fixtures

`regress/udp/systemd-fixture.conf`, `ssh-udp-fixture.service`,
`transport-fixture.py` and `slow-resolver.c` are **disposable VM fixtures**,
not production configurations. They allow test root login, use a fixture key
path, and preload a deliberately slow resolver for `slow.test`.

Compile the interposer with `cc -shared -fPIC slow-resolver.c -ldl -o
slow-resolver.so`. The transport fixture provides TCP/UDP echo on loopback
29001 and SOCKS5 on 29080; `/tmp/ssh-udp-slow-relay` selects a slow relay name.
The paired sing-box tests use `SSH_UDPGW_TEST_PROFILE` with an explicit
`disposableFixture: true`, loopback SSH port, username, key path and pinned
host key. `SSH_UDP_NATIVE_SLOW=direct|proxy|relay` selects deadline tests;
configure the matching direct target, proxy hostname or relay fixture first.
Never use these fixtures against a production daemon or reuse their keys.
