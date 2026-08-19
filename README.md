# Portable OpenSSH

[![C/C++ CI](../../actions/workflows/c-cpp.yml/badge.svg)](../../actions/workflows/c-cpp.yml)
[![VM CI](../../actions/workflows/vm.yml/badge.svg)](../../actions/workflows/vm.yml)
[![CIFuzz](../../actions/workflows/cifuzz.yml/badge.svg)](../../actions/workflows/cifuzz.yml)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/openssh.svg)](https://issues.oss-fuzz.com/issues?q="Project:+openssh"+is:open)
[![Coverity Status](https://scan.coverity.com/projects/21341/badge.svg)](https://scan.coverity.com/projects/openssh-portable)

OpenSSH is a complete implementation of the SSH protocol (version 2) for secure remote login, command execution and file transfer. It includes a client ``ssh`` and server ``sshd``, file transfer utilities ``scp`` and ``sftp`` as well as tools for key generation (``ssh-keygen``), run-time key storage (``ssh-agent``) and a number of supporting programs.

This is a port of OpenBSD's [OpenSSH](https://openssh.com) to most Unix-like operating systems, including Linux, OS X and Cygwin. Portable OpenSSH polyfills OpenBSD APIs that are not available elsewhere, adds sshd sandboxing for more operating systems and includes support for OS-native authentication and auditing (e.g. using PAM).

## Documentation

The official documentation for OpenSSH are the man pages for each tool:

* [ssh(1)](https://man.openbsd.org/ssh.1)
* [sshd(8)](https://man.openbsd.org/sshd.8)
* [ssh-keygen(1)](https://man.openbsd.org/ssh-keygen.1)
* [ssh-agent(1)](https://man.openbsd.org/ssh-agent.1)
* [scp(1)](https://man.openbsd.org/scp.1)
* [sftp(1)](https://man.openbsd.org/sftp.1)
* [ssh-keyscan(8)](https://man.openbsd.org/ssh-keyscan.8)
* [sftp-server(8)](https://man.openbsd.org/sftp-server.8)

## Stable Releases

Stable release tarballs are available from a number of [download mirrors](https://www.openssh.com/portable.html#downloads). We recommend the use of a stable release for most users. Please read the [release notes](https://www.openssh.com/releasenotes.html) for details of recent changes and potential incompatibilities.

## Building Portable OpenSSH

### Dependencies

Portable OpenSSH is built using autoconf and make. It requires a working C compiler, standard library and headers.

``libcrypto`` from either [LibreSSL](https://www.libressl.org/) or [OpenSSL](https://www.openssl.org) may also be used.  OpenSSH may be built without either of these, but the resulting binaries will have only a subset of the cryptographic algorithms normally available.

[zlib](https://www.zlib.net/) is optional; without it transport compression is not supported.

FIDO security token support needs [libfido2](https://github.com/Yubico/libfido2) and its dependencies and will be enabled automatically if they are found.

In addition, certain platforms and build-time options may require additional dependencies; see README.platform for details about your platform.

### Building a release

Release tarballs and release branches in git include a pre-built copy of the ``configure`` script and may be built using:

```
tar zxvf openssh-X.YpZ.tar.gz
cd openssh
./configure # [options]
make && make tests
```

See the [Build-time Customisation](#build-time-customisation) section below for configure options. If you plan on installing OpenSSH to your system, then you will usually want to specify destination paths.

### Building from git

If building from the git master branch, you'll need [autoconf](https://www.gnu.org/software/autoconf/) installed to build the ``configure`` script. The following commands will check out and build portable OpenSSH from git:

```
git clone https://github.com/openssh/openssh-portable # or https://anongit.mindrot.org/openssh.git
cd openssh-portable
autoreconf
./configure
make && make tests
```

### Running targeted forwarding regress tests

When you only want to validate dynamic forwarding paths (including
``ForwardProxy``), you can run just these tests against the binaries in
your current build tree:

```
make -C regress t-exec \
	.CURDIR="$PWD/regress" .OBJDIR="$PWD/regress" OBJ="$PWD/regress" \
	TEST_SHELL=sh \
	LTESTS='dynamic-forward forward-http-proxy' \
	TEST_SSH_UNSAFE_PERMISSIONS=1 \
	TEST_SSH_SSH="$PWD/ssh" \
	TEST_SSH_SSHD="$PWD/sshd" \
	TEST_SSH_SSHD_SESSION="$PWD/sshd-session" \
	TEST_SSH_SSHD_AUTH="$PWD/sshd-auth" \
	TEST_SSH_SSHAGENT="$PWD/ssh-agent" \
	TEST_SSH_SSHADD="$PWD/ssh-add" \
	TEST_SSH_SSHKEYGEN="$PWD/ssh-keygen" \
	TEST_SSH_SSHKEYSCAN="$PWD/ssh-keyscan" \
	TEST_SSH_SFTP="$PWD/sftp" \
	TEST_SSH_SFTPSERVER="$PWD/sftp-server" \
	TEST_SSH_SCP="$PWD/scp"
```

The ``TEST_SSH_*`` overrides ensure the regress harness uses your freshly built
binaries instead of any system-installed OpenSSH.

If you prefer a compact one-liner, use:

```
BIN="$PWD"; make -C regress t-exec .CURDIR="$PWD/regress" .OBJDIR="$PWD/regress" OBJ="$PWD/regress" TEST_SHELL=sh LTESTS='dynamic-forward forward-http-proxy' TEST_SSH_UNSAFE_PERMISSIONS=1 TEST_SSH_SSH="$BIN/ssh" TEST_SSH_SSHD="$BIN/sshd" TEST_SSH_SSHD_SESSION="$BIN/sshd-session" TEST_SSH_SSHD_AUTH="$BIN/sshd-auth" TEST_SSH_SSHAGENT="$BIN/ssh-agent" TEST_SSH_SSHADD="$BIN/ssh-add" TEST_SSH_SSHKEYGEN="$BIN/ssh-keygen" TEST_SSH_SSHKEYSCAN="$BIN/ssh-keyscan" TEST_SSH_SFTP="$BIN/sftp" TEST_SSH_SFTPSERVER="$BIN/sftp-server" TEST_SSH_SCP="$BIN/scp"
```

To run only the ``forward-http-proxy`` test:

```
BIN="$PWD"; make -C regress t-exec .CURDIR="$PWD/regress" .OBJDIR="$PWD/regress" OBJ="$PWD/regress" TEST_SHELL=sh LTESTS='forward-http-proxy' TEST_SSH_UNSAFE_PERMISSIONS=1 TEST_SSH_SSH="$BIN/ssh" TEST_SSH_SSHD="$BIN/sshd" TEST_SSH_SSHD_SESSION="$BIN/sshd-session" TEST_SSH_SSHD_AUTH="$BIN/sshd-auth" TEST_SSH_SSHAGENT="$BIN/ssh-agent" TEST_SSH_SSHADD="$BIN/ssh-add" TEST_SSH_SSHKEYGEN="$BIN/ssh-keygen" TEST_SSH_SSHKEYSCAN="$BIN/ssh-keyscan" TEST_SSH_SFTP="$BIN/sftp" TEST_SSH_SFTPSERVER="$BIN/sftp-server" TEST_SSH_SCP="$BIN/scp"
```

### ForwardProxy update summary

This repository now includes server-side support for server-side forwarding
via upstream proxies using ``ForwardProxy``.

Highlights:

* Added ``ForwardProxy`` parsing and config plumbing in the server config
	path.
* Added server channel connect support for HTTP ``CONNECT`` and SOCKS
	(``socks4``/``socks5``) tunneling.
* Wired ``direct-tcpip`` handling to use the configured upstream proxy when
	``ForwardProxy`` is set.
* Documented ``ForwardProxy`` in ``sshd_config(5)``.
* Added a regress test, ``regress/forward-http-proxy.sh``, that validates
	dynamic forwarding through a local HTTP CONNECT proxy.

Scope note:

* This is implemented at server ``direct-tcpip`` channel handling scope.
	Therefore, it applies to ``direct-tcpip`` channels handled by ``sshd``
	(including dynamic forwarding traffic), rather than introducing a protocol
	marker that uniquely identifies ``-D`` at the server.

Validation performed:

* ``make tests`` completed successfully in this environment.
* Targeted regress runs passed for both
	``dynamic-forward`` and ``forward-http-proxy``.

Usage example:

1) Configure server-side proxying in ``sshd_config`` (``http``/``socks4``/
   ``socks5``):

```
AllowTcpForwarding yes
ForwardProxy http://127.0.0.1:3081
```

	If your HTTP proxy requires authentication, include credentials:

```
AllowTcpForwarding yes
ForwardProxy http://proxyuser:proxypass@127.0.0.1:3081
```

	This sends a ``Proxy-Authorization: Basic ...`` header on ``CONNECT``.

	For SOCKS4/SOCKS5, switch by scheme:

```
AllowTcpForwarding yes
ForwardProxy socks4://proxyuser@127.0.0.1:1080
```

```
AllowTcpForwarding yes
ForwardProxy socks5://proxyuser:proxypass@127.0.0.1:1080
```

2) Restart ``sshd`` and create a dynamic SOCKS tunnel from the client:

```
ssh -D 127.0.0.1:1080 -N user@server
```

3) Send traffic through the SOCKS tunnel (example with ``curl``):

```
curl --socks5-hostname 127.0.0.1:1080 http://example.com/
```

With ``ForwardProxy`` set, server handling of ``direct-tcpip`` channels
(including traffic from ``-D`` dynamic forwarding) will egress through the
configured HTTP or SOCKS proxy.

### Transparent SSH relay

A dedicated ``sshd`` instance may relay its complete incoming TCP stream to a
fixed second SSH server before any SSH handshake takes place:

```
Port 11111
PidFile /run/sshd-relay.pid
SSHRelayTarget [2001:db8::b]:22
SSHRelayConnectTimeout 10s
```

The client connects to the relay address but sees the target server's host key
and authenticates directly to the target.  The relay never receives or
replays the SSH username, password, private-key signature or channel data.
Use another ``sshd`` instance for administrative access to the relay host.

On the target server, ``ForwardProxy`` may then proxy ``direct-tcpip`` traffic
created by ``ssh -D`` or ``ssh -L``:

```
AllowTcpForwarding yes
ForwardProxy socks5://proxyuser:proxypass@127.0.0.1:1080
```

Ordinary shell, exec, SCP and SFTP traffic terminates at the target SSH server;
only forwarding channels are sent through ``ForwardProxy``.

Running ``sshd`` with a specific config file:

``sshd`` supports an explicit config path via ``-f``.

1) Validate config syntax before starting:

```
./sshd -t -f /path/to/sshd_config
```

2) Run in foreground (useful for debugging):

```
./sshd -D -e -f /path/to/sshd_config
```

3) Run with default daemon behaviour but custom config file:

```
./sshd -f /path/to/sshd_config
```

``sshd_config`` template (baseline/default style):

The following example is a practical baseline template you can copy to start
from (adjust paths/users for your system):

```
# Network
Port 22
AddressFamily any
ListenAddress 0.0.0.0
ListenAddress ::

# Host keys (use the paths generated on your host)
HostKey /etc/ssh/ssh_host_ed25519_key
HostKey /etc/ssh/ssh_host_rsa_key

# Authentication
PermitRootLogin prohibit-password
PubkeyAuthentication yes
PasswordAuthentication yes
KbdInteractiveAuthentication yes
UsePAM yes

# Session/security defaults
X11Forwarding no
PermitEmptyPasswords no
ChallengeResponseAuthentication no
PrintMotd no

# Forwarding
AllowTcpForwarding yes
AllowAgentForwarding yes
AllowStreamLocalForwarding yes
# Upstream proxy for direct-tcpip (server-side forwarding egress)
# Default disabled:
ForwardProxy none
#
# Enable with one of the following formats:
#   ForwardProxy http://127.0.0.1:3081
#   ForwardProxy socks4://127.0.0.1:1080
#   ForwardProxy socks5://127.0.0.1:1080
#
# With authentication:
#   HTTP Basic auth:
#     ForwardProxy http://proxyuser:proxypass@127.0.0.1:3081
#   SOCKS4 user field:
#     ForwardProxy socks4://proxyuser@127.0.0.1:1080
#   SOCKS5 username/password auth:
#     ForwardProxy socks5://proxyuser:proxypass@127.0.0.1:1080
#
# Legacy alias (kept for compatibility, maps to ForwardProxy):
#   ForwardHttpProxy 127.0.0.1:3081

# Keepalive
ClientAliveInterval 0
ClientAliveCountMax 3

# SFTP subsystem
Subsystem sftp /usr/local/libexec/sftp-server
```

Test template before reload/restart:

```
./sshd -t -f /path/to/sshd_config
```

Running ``sshd`` via ``systemd``:

You can create a dedicated service unit that points to a specific config file.
Example ``/etc/systemd/system/sshd-custom.service``:

```
[Unit]
Description=OpenSSH server daemon (custom config)
After=network.target

[Service]
Type=notify
ExecStart=/usr/local/sbin/sshd -D -e -f /path/to/sshd_config
ExecReload=/bin/kill -HUP $MAINPID
KillMode=process
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
```

Apply and manage:

```
sudo systemctl daemon-reload
sudo systemctl enable --now sshd-custom.service
sudo systemctl status sshd-custom.service
sudo systemctl restart sshd-custom.service
```

### Build-time Customisation

There are many build-time customisation options available. All Autoconf destination path flags (e.g. ``--prefix``) are supported (and are usually required if you want to install OpenSSH).

For a full list of available flags, run ``./configure --help`` but a few of the more frequently-used ones are described below. Some of these flags will require additional libraries and/or headers be installed.

Flag | Meaning
--- | ---
``--with-pam`` | Enable [PAM](https://en.wikipedia.org/wiki/Pluggable_authentication_module) support. [OpenPAM](https://www.openpam.org/), [Linux PAM](http://www.linux-pam.org/) and Solaris PAM are supported.
``--with-libedit`` | Enable [libedit](https://www.thrysoee.dk/editline/) support for sftp.
``--with-kerberos5`` | Enable Kerberos/GSSAPI support. Both [Heimdal](https://www.h5l.org/) and [MIT](https://web.mit.edu/kerberos/) Kerberos implementations are supported.
``--with-selinux`` | Enable [SELinux](https://en.wikipedia.org/wiki/Security-Enhanced_Linux) support.

## Development

Portable OpenSSH development is discussed on the [openssh-unix-dev mailing list](https://lists.mindrot.org/mailman/listinfo/openssh-unix-dev) ([archive mirror](https://marc.info/?l=openssh-unix-dev)). Bugs and feature requests are tracked on our [Bugzilla](https://bugzilla.mindrot.org/).

## Reporting bugs

_Non-security_ bugs may be reported to the developers via [Bugzilla](https://bugzilla.mindrot.org/) or via the mailing list above. Security bugs should be reported to [openssh@openssh.com](mailto:openssh.openssh.com).
