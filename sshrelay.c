/* Placed in the Public Domain. */

#include "includes.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "misc.h"
#include "sshrelay.h"

#define SSH_RELAY_BUFSIZE (64 * 1024)

struct relay_direction {
	int src;
	int dst;
	u_char buf[SSH_RELAY_BUFSIZE];
	size_t off;
	size_t len;
	int eof;
	int shut;
	uint64_t transferred;
};

int
ssh_relay_connect(const char *target, int timeout_seconds, int keepalive,
    char **hostp, int *portp)
{
	struct addrinfo hints, *aitop = NULL, *ai;
	char *user = NULL, *host = NULL, strport[NI_MAXSERV];
	int gaierr, oerrno = EHOSTUNREACH, port = -1, sock = -1, timeout_ms;
	int on = 1;

	if (hostp != NULL)
		*hostp = NULL;
	if (portp != NULL)
		*portp = -1;
	if (parse_user_host_port(target, &user, &host, &port) != 0 ||
	    user != NULL || port <= 0) {
		errno = EINVAL;
		goto out;
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(strport, sizeof(strport), "%d", port);
	if ((gaierr = getaddrinfo(host, strport, &hints, &aitop)) != 0) {
		error("relay target %.200s: %s", host, ssh_gai_strerror(gaierr));
		errno = EHOSTUNREACH;
		goto out;
	}
	timeout_ms = timeout_seconds >= INT_MAX / 1000 ?
	    INT_MAX : timeout_seconds * 1000;
	for (ai = aitop; ai != NULL; ai = ai->ai_next) {
		if ((sock = socket(ai->ai_family, ai->ai_socktype,
		    ai->ai_protocol)) == -1) {
			oerrno = errno;
			continue;
		}
		if (timeout_connect(sock, ai->ai_addr, ai->ai_addrlen,
		    &timeout_ms) == 0)
			break;
		oerrno = errno;
		close(sock);
		sock = -1;
		if (timeout_ms == 0)
			break;
	}
	if (sock == -1) {
		errno = oerrno;
		goto out;
	}
	set_nodelay(sock);
	if (keepalive && setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
	    &on, sizeof(on)) == -1)
		error("setsockopt relay target SO_KEEPALIVE: %s", strerror(errno));
	if (hostp != NULL) {
		*hostp = host;
		host = NULL;
	}
	if (portp != NULL)
		*portp = port;
 out:
	if (aitop != NULL)
		freeaddrinfo(aitop);
	free(user);
	free(host);
	return sock;
}

static int
relay_read(struct relay_direction *d)
{
	ssize_t n;

	if (d->off != 0 && d->off + d->len == sizeof(d->buf)) {
		memmove(d->buf, d->buf + d->off, d->len);
		d->off = 0;
	}
	n = read(d->src, d->buf + d->off + d->len,
	    sizeof(d->buf) - d->off - d->len);
	if (n > 0) {
		d->len += (size_t)n;
		return 0;
	}
	if (n == 0) {
		d->eof = 1;
		return 0;
	}
	if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
		return 0;
	return -1;
}

static int
relay_write(struct relay_direction *d)
{
	ssize_t n;

	n = write(d->dst, d->buf + d->off, d->len);
	if (n > 0) {
		d->off += (size_t)n;
		d->len -= (size_t)n;
		d->transferred += (uint64_t)n;
		if (d->len == 0)
			d->off = 0;
		return 0;
	}
	if (n == -1 && (errno == EINTR || errno == EAGAIN ||
	    errno == EWOULDBLOCK))
		return 0;
	return -1;
}

int
ssh_relay_loop(int client, int upstream, uint64_t *client_to_upstream,
    uint64_t *upstream_to_client)
{
	struct relay_direction d[2];
	struct pollfd pfd[2];
	int i, r;

	memset(d, 0, sizeof(d));
	d[0].src = client;
	d[0].dst = upstream;
	d[1].src = upstream;
	d[1].dst = client;
	if (set_nonblock(client) == -1 || set_nonblock(upstream) == -1)
		return -1;
	for (;;) {
		memset(pfd, 0, sizeof(pfd));
		pfd[0].fd = client;
		pfd[1].fd = upstream;
		for (i = 0; i < 2; i++) {
			if (!d[i].eof && d[i].len < sizeof(d[i].buf))
				pfd[d[i].src == client ? 0 : 1].events |= POLLIN;
			if (d[i].len != 0)
				pfd[d[i].dst == client ? 0 : 1].events |= POLLOUT;
			if (d[i].eof && d[i].len == 0 && !d[i].shut) {
				if (shutdown(d[i].dst, SHUT_WR) == -1 &&
				    errno != ENOTCONN && errno != EINVAL)
					return -1;
				d[i].shut = 1;
			}
		}
		if (d[0].shut && d[1].shut)
			break;
		do {
			r = poll(pfd, 2, -1);
		} while (r == -1 && errno == EINTR);
		if (r == -1)
			return -1;
		if ((pfd[0].revents | pfd[1].revents) & POLLNVAL) {
			errno = EBADF;
			return -1;
		}
		for (i = 0; i < 2; i++) {
			if (d[i].len != 0 &&
			    (pfd[d[i].dst == client ? 0 : 1].revents &
			    POLLOUT) &&
			    relay_write(&d[i]) == -1)
				return -1;
			if (!d[i].eof && d[i].len < sizeof(d[i].buf) &&
			    (pfd[d[i].src == client ? 0 : 1].revents &
			    (POLLIN | POLLHUP | POLLERR)) &&
			    relay_read(&d[i]) == -1)
				return -1;
		}
	}
	if (client_to_upstream != NULL)
		*client_to_upstream = d[0].transferred;
	if (upstream_to_client != NULL)
		*upstream_to_client = d[1].transferred;
	return 0;
}
