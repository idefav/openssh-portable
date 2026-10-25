/*
 * UDP datagrams over an ordinary SSH byte-stream channel.
 * Distributed under the same BSD licence as channels.c.
 */
#include "includes.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ssh.h"
#include "ssh2.h"
#include "sshbuf.h"
#include "packet.h"
#include "channels.h"
#include "udp-channel.h"
#include "log.h"
#include "misc.h"
#include "xmalloc.h"

#define UDP_MAX 65507
#define UDP_WINDOW (128 * 1024)
enum { U_CONNECT, U_GREETING_SEND, U_GREETING_RECV, U_AUTH_SEND,
    U_AUTH_RECV, U_ASSOC_SEND, U_ASSOC_RECV, U_READY };

struct udp_channel {
	int state, proxy;
	char *user, *password;
	u_char tx[600], rx[600], header[262];
	size_t txlen, sent, rxlen, headerlen;
	time_t deadline;
	struct addrinfo *addresses, *next;
};

static int
udp_next_socket(struct udp_channel *u, int type)
{
	struct addrinfo *p;
	int fd = -1;
	while ((p = u->next) != NULL) {
		u->next = p->ai_next;
		if ((fd = socket(p->ai_family, type, 0)) == -1)
			continue;
		if (set_nonblock(fd) == 0 && (connect(fd, p->ai_addr,
		    p->ai_addrlen) == 0 || errno == EINPROGRESS))
			break;
		close(fd);
		fd = -1;
	}
	return fd;
}

static int
udp_socket(const char *host, int port, int type, struct udp_channel *saved)
{
	struct udp_channel temporary;
	struct udp_channel *u = saved;
	struct addrinfo hints;
	char service[16];
	int fd;
	memset(&temporary, 0, sizeof(temporary));
	if (u == NULL) u = &temporary;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = type;
	snprintf(service, sizeof(service), "%d", port);
	if (getaddrinfo(host, service, &hints, &u->addresses) != 0)
		return -1;
	u->next = u->addresses;
	fd = udp_next_socket(u, type);
	if (saved == NULL) freeaddrinfo(u->addresses);
	return fd;
}

static void
udp_fail(struct ssh *ssh, Channel *c, const char *message)
{
	int r;
	debug("channel %d: UDP: %s", c->self, message);
	c->io_want = 0;
	if (c->type == SSH_CHANNEL_CONNECTING) {
		if ((r = sshpkt_start(ssh, SSH2_MSG_CHANNEL_OPEN_FAILURE)) != 0 ||
		    (r = sshpkt_put_u32(ssh, c->remote_id)) != 0 ||
		    (r = sshpkt_put_u32(ssh, SSH2_OPEN_CONNECT_FAILED)) != 0 ||
		    (r = sshpkt_put_cstring(ssh, message)) != 0 ||
		    (r = sshpkt_put_cstring(ssh, "")) != 0 ||
		    (r = sshpkt_send(ssh)) != 0)
			sshpkt_fatal(ssh, r, "UDP open failure");
		chan_mark_dead(ssh, c);
	} else {
		channel_force_close(ssh, c, 0);
	}
}

static void
udp_confirm(struct ssh *ssh, Channel *c)
{
	int r;
	c->udp->state = U_READY;
	if (c->udp->addresses != NULL) {
		freeaddrinfo(c->udp->addresses);
		c->udp->addresses = c->udp->next = NULL;
	}
	c->udp->deadline = monotime() + 60;
	c->type = SSH_CHANNEL_OPEN;
	if ((r = sshpkt_start(ssh, SSH2_MSG_CHANNEL_OPEN_CONFIRMATION)) != 0 ||
	    (r = sshpkt_put_u32(ssh, c->remote_id)) != 0 ||
	    (r = sshpkt_put_u32(ssh, c->self)) != 0 ||
	    (r = sshpkt_put_u32(ssh, c->local_window)) != 0 ||
	    (r = sshpkt_put_u32(ssh, c->local_maxpacket)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		sshpkt_fatal(ssh, r, "UDP open confirmation");
}

Channel *
udp_channel_new(struct ssh *ssh, const char *host, u_short port,
    const char *proxy, int proxyport, const char *user, const char *password)
{
	Channel *c;
	struct udp_channel *u;
	struct in_addr v4;
	struct in6_addr v6;
	int fd;
	size_t n;
	if ((user != NULL && strlen(user) > 255) ||
	    (password != NULL && strlen(password) > 255))
		return NULL;
	u = xcalloc(1, sizeof(*u));
	fd = udp_socket(proxy == NULL ? host : proxy,
	    proxy == NULL ? port : proxyport,
	    proxy == NULL ? SOCK_DGRAM : SOCK_STREAM, u);
	if (fd == -1) {
		if (u->addresses != NULL) freeaddrinfo(u->addresses);
		free(u);
		return NULL;
	}
	c = channel_new(ssh, SSH_UDP_CHANNEL, SSH_CHANNEL_CONNECTING,
	    fd, fd, -1, UDP_WINDOW, CHAN_TCP_PACKET_DEFAULT,
	    CHAN_EXTENDED_IGNORE, "UDP forward", 1);
	c->path = xstrdup(host);
	c->host_port = port;
	c->udp = u;
	u->proxy = proxy != NULL;
	u->user = user == NULL ? NULL : xstrdup(user);
	u->password = password == NULL ? NULL : xstrdup(password);
	u->deadline = monotime() + 3;
	/* RFC 1928 header, including the three reserved/fragment bytes. */
	n = 3;
	if (inet_pton(AF_INET, host, &v4) == 1) {
		u->header[n++] = 1;
		memcpy(u->header + n, &v4, 4); n += 4;
	} else if (inet_pton(AF_INET6, host, &v6) == 1) {
		u->header[n++] = 4;
		memcpy(u->header + n, &v6, 16); n += 16;
	} else {
		u->header[n++] = 3;
		u->header[n++] = strlen(host);
		memcpy(u->header + n, host, strlen(host)); n += strlen(host);
	}
	u->header[n++] = port >> 8;
	u->header[n++] = port & 255;
	u->headerlen = n;
	return c;
}

void
udp_channel_free(Channel *c)
{
	struct udp_channel *u = c->udp;
	if (u == NULL) return;
	if (u->addresses != NULL) freeaddrinfo(u->addresses);
	if (u->user != NULL) freezero(u->user, strlen(u->user));
	if (u->password != NULL) freezero(u->password, strlen(u->password));
	freezero(u, sizeof(*u));
	c->udp = NULL;
}

time_t
udp_channel_expiry(Channel *c)
{
	return c->udp->deadline;
}

void
udp_channel_pre(struct ssh *ssh, Channel *c)
{
	struct udp_channel *u = c->udp;
	u_int len;
	c->io_want = 0;
	if (monotime() >= u->deadline) {
		udp_fail(ssh, c, "UDP timeout");
		return;
	}
	if (u->state != U_READY) {
		c->io_want = (u->state == U_CONNECT || (u->state & 1)) ?
		    SSH_CHAN_IO_WFD : SSH_CHAN_IO_RFD;
		return;
	}
	if (c->istate != CHAN_INPUT_OPEN || c->ostate != CHAN_OUTPUT_OPEN) {
		udp_fail(ssh, c, "UDP channel closed");
		return;
	}
	/* Keep at most one maximum-sized pending frame plus one incoming frame. */
	if (sshbuf_len(c->input) < UDP_MAX + 4)
		c->io_want |= SSH_CHAN_IO_RFD;
	if (sshbuf_len(c->output) >= 4) {
		len = PEEK_U32(sshbuf_ptr(c->output));
		if (len > UDP_MAX) {
			udp_fail(ssh, c, "UDP frame too large");
			return;
		}
		if (sshbuf_len(c->output) >= len + 4)
			c->io_want |= SSH_CHAN_IO_WFD;
	}
	if (c->efd != -1)
		c->io_want |= SSH_CHAN_IO_EFD_R;
}

static void
udp_associate(struct udp_channel *u)
{
	static const u_char request[] = {5, 3, 0, 1, 0, 0, 0, 0, 0, 0};
	memcpy(u->tx, request, sizeof(request));
	u->txlen = sizeof(request);
	u->sent = u->rxlen = 0;
	u->state = U_ASSOC_SEND;
}

/* Return the SOCKS address end offset, including port, or 0 for incomplete. */
static int
udp_address_end(const u_char *p, size_t len, size_t atyp)
{
	size_t end;
	if (len <= atyp) return 0;
	switch (p[atyp]) {
	case 1: end = atyp + 1 + 4 + 2; break;
	case 4: end = atyp + 1 + 16 + 2; break;
	case 3:
		if (len <= atyp + 1) return 0;
		if (p[atyp + 1] == 0) return -1;
		end = atyp + 2 + p[atyp + 1] + 2; break;
	default: return -1;
	}
	return len < end ? 0 : (int)end;
}

static int
udp_relay_socket(Channel *c, int end)
{
	struct udp_channel *u = c->udp;
	char host[NI_MAXHOST];
	struct sockaddr_storage peer;
	socklen_t plen = sizeof(peer);
	int port = (u->rx[end-2] << 8) | u->rx[end-1];
	const u_char *p = u->rx + 4;
	int unspecified = 0;
	size_t i, n = u->rx[3] == 1 ? 4 : 16;
	if (port == 0) return -1;
	if (u->rx[3] == 3) {
		memcpy(host, p+1, p[0]); host[p[0]] = '\0';
		if (strlen(host) != p[0]) return -1;
	} else {
		for (i = 0, unspecified = 1; i < n; i++)
			if (p[i] != 0) unspecified = 0;
		if (!unspecified && inet_ntop(u->rx[3] == 1 ? AF_INET : AF_INET6,
		    p, host, sizeof(host)) == NULL) return -1;
		if (unspecified && (getpeername(c->sock, (struct sockaddr *)&peer,
		    &plen) == -1 || getnameinfo((struct sockaddr *)&peer, plen,
		    host, sizeof(host), NULL, 0, NI_NUMERICHOST) != 0)) return -1;
	}
	return udp_socket(host, port, SOCK_DGRAM, NULL);
}

static void
udp_handshake(struct ssh *ssh, Channel *c)
{
	struct udp_channel *u = c->udp;
	ssize_t n;
	int err = 0, end, fd;
	socklen_t elen = sizeof(err);
	size_t ul, pl;
	if (u->state == U_CONNECT) {
		if (!(c->io_ready & SSH_CHAN_IO_WFD)) return;
		if (getsockopt(c->sock, SOL_SOCKET, SO_ERROR, &err, &elen) == -1 || err != 0) {
			fd = udp_next_socket(u, u->proxy ? SOCK_STREAM : SOCK_DGRAM);
			if (fd == -1) goto fail;
			close(c->sock);
			c->sock = c->rfd = c->wfd = fd;
			return;
		}
		if (!u->proxy) { udp_confirm(ssh, c); return; }
		u->tx[0] = 5; u->tx[1] = 1; u->tx[2] = u->user == NULL ? 0 : 2;
		u->txlen = 3; u->state = U_GREETING_SEND;
	}
	if (u->state & 1) {
		if (!(c->io_ready & SSH_CHAN_IO_WFD)) return;
		n = write(c->sock, u->tx + u->sent, u->txlen - u->sent);
		if (n == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return;
		if (n <= 0) goto fail;
		u->sent += n;
		if (u->sent == u->txlen) { u->state++; u->rxlen = 0; }
		return;
	}
	if (!(c->io_ready & SSH_CHAN_IO_RFD)) return;
	n = read(c->sock, u->rx + u->rxlen, sizeof(u->rx) - u->rxlen);
	if (n == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return;
	if (n <= 0) goto fail;
	u->rxlen += n;
	if (u->rxlen < 2) return;
	if (u->state == U_GREETING_RECV) {
		if (u->rxlen != 2 || u->rx[0] != 5 ||
		    u->rx[1] != (u->user == NULL ? 0 : 2)) goto fail;
		if (u->user == NULL) { udp_associate(u); return; }
		ul = strlen(u->user); pl = u->password == NULL ? 0 : strlen(u->password);
		u->tx[0] = 1; u->tx[1] = ul; memcpy(u->tx+2, u->user, ul);
		u->tx[ul+2] = pl;
		if (pl != 0) memcpy(u->tx+ul+3, u->password, pl);
		u->txlen = ul+pl+3; u->sent = 0; u->state = U_AUTH_SEND;
	} else if (u->state == U_AUTH_RECV) {
		if (u->rxlen != 2 || u->rx[0] != 1 || u->rx[1] != 0) goto fail;
		udp_associate(u);
	} else {
		if (u->rxlen < 4) return;
		if (u->rx[0] != 5 || u->rx[1] != 0 || u->rx[2] != 0) goto fail;
		end = udp_address_end(u->rx, u->rxlen, 3);
		if (end == 0) return;
		if (end < 0 || u->rxlen != (size_t)end) goto fail;
		if ((fd = udp_relay_socket(c, end)) == -1) goto fail;
		c->efd = c->sock; /* Keep TCP association alive and poll for closure. */
		c->sock = c->rfd = c->wfd = fd;
		udp_confirm(ssh, c);
	}
	return;
 fail:
	udp_fail(ssh, c, "UDP upstream setup failed");
}

void
udp_channel_post(struct ssh *ssh, Channel *c)
{
	struct udp_channel *u = c->udp;
	u_char packet[65535];
	const u_char *p;
	struct iovec iov;
	struct msghdr msg;
	ssize_t n;
	size_t len, offset;
	int r, end;
	if (u->state != U_READY) { udp_handshake(ssh, c); return; }
	if (c->io_ready & SSH_CHAN_IO_EFD_R) {
		n = read(c->efd, packet, 1);
		if (n >= 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
			udp_fail(ssh, c, "UDP upstream control closed"); return;
		}
	}
	if (c->io_ready & SSH_CHAN_IO_RFD) {
		memset(&msg, 0, sizeof(msg));
		iov.iov_base = packet; iov.iov_len = sizeof(packet);
		msg.msg_iov = &iov; msg.msg_iovlen = 1;
		n = recvmsg(c->rfd, &msg, 0);
		if (n >= 0 && !(msg.msg_flags & MSG_TRUNC)) {
			offset = 0;
			if (u->proxy) {
				if (n < 4 || packet[0] || packet[1] || packet[2]) goto write_packet;
				end = udp_address_end(packet, n, 3);
				if (end <= 0 || ((packet[end-2] << 8) | packet[end-1]) != c->host_port)
					goto write_packet;
				/* Numeric destinations must also match the reported source. */
				if (u->header[3] != 3 && (end != (int)u->headerlen ||
				    memcmp(packet+3, u->header+3, end-3) != 0)) goto write_packet;
				offset = end;
			}
			len = n - offset;
			if (len <= UDP_MAX) {
				if ((r = sshbuf_put_string(c->input, packet+offset, len)) != 0)
					fatal("UDP receive buffer: %d", r);
				u->deadline = monotime() + 60;
			}
		}
	}
 write_packet:
	if (!(c->io_ready & SSH_CHAN_IO_WFD) || sshbuf_len(c->output) < 4) return;
	p = sshbuf_ptr(c->output);
	len = PEEK_U32(p);
	if (len > UDP_MAX) { udp_fail(ssh, c, "UDP frame too large"); return; }
	if (sshbuf_len(c->output) < len + 4) return;
	p += 4;
	offset = u->proxy ? u->headerlen : 0;
	if (len + offset <= UDP_MAX) {
		if (offset != 0) memcpy(packet, u->header, offset);
		memcpy(packet + offset, p, len);
		n = send(c->wfd, packet, len + offset, 0);
		if (n == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return;
		if (n == (ssize_t)(len + offset)) u->deadline = monotime() + 60;
		else debug("channel %d: UDP send dropped: %s", c->self, strerror(errno));
	} else debug("channel %d: UDP SOCKS5 datagram too large", c->self);
	/* Window accounting includes the explicit framing bytes, even on drops. */
	if ((r = sshbuf_consume(c->output, len + 4)) != 0)
		fatal("UDP consume: %d", r);
	c->local_consumed += len + 4;
}
