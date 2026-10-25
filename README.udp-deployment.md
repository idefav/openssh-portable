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
