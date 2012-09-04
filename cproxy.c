#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include "cluster.h"

#define CHATTER_TIMEOUT	120
#define BRINGUP_TIMEOUT	30

unsigned char buffer[32768];
unsigned int bufferx = 0;

static int bufferin(int fd)
{
	int r;

	if (fd == -1) return 0;
	do {
		r = read(fd, buffer, sizeof(buffer));
	} while (r == -1 && errno == EINTR);
	if (r == -1 && errno == EAGAIN) {
		bufferx = 0;
		return 1;
	}
	if (r < 1) return 0;
	bufferx = r;
	return 1;
}
static int bufferout(int fd)
{
	int r, x;

	if (fd == -1) return 0;
	for (x = 0; x < bufferx;) {
		do {
			r = write(fd, buffer+x, bufferx-x);
		} while (r == -1 && (errno == EINTR || errno == EAGAIN));
		if (r < 1) return 0; /* blech */
		x += r;
	}
	return 1;
}


struct cpipe {
	int left, right;
	struct sockaddr_in todo;
	time_t expires;

	struct cpipe *next;
	int state;
};

static struct cpipe *top = 0;

static struct cpipe *add_cpipe(int fd, struct sockaddr_in *ct)
{
	struct cpipe *x;
	int j;

	/* check cluster list and don't create a new cpipe; instead return
	 * an existing one...
	 */
	for (x = top; x; x = x->next) {
		if (x->left == -1) continue;
		if (x->todo.sin_family != ct->sin_family) continue;
		if (x->todo.sin_port != ct->sin_port) continue;
		if (memcmp(&x->todo.sin_addr, &ct->sin_addr,
					sizeof(ct->sin_addr)) != 0) continue;
		/* found a dup */

		/* already accepted/connected */
		if (x->state != 0) return 0;

		/* increase timeout */
		x->expires = time(0) + BRINGUP_TIMEOUT;
		return x;
	}


	x = (struct cpipe *)malloc(sizeof(struct cpipe));
	if (!x) return 0;

	x->left = fd;
	x->right = -1;
	memcpy(&x->todo, ct, sizeof(struct sockaddr_in));
	x->expires = time(0) + BRINGUP_TIMEOUT;

	x->state = 0;
	x->next = top;
	top = x;

	return x;
}
static int select_cpipes(int maxfd, fd_set *r, fd_set *w)
{
	register struct cpipe *c;

	for (c = top; c; c = c->next) {
		/* busy state */
		if (c->left > -1 && c->state != 2) {
			FD_SET(c->left, r);
			if (c->left > maxfd) maxfd = c->left;
		}
		if (c->right > -1 && c->state != 3) {
			if (c->state == 1) {
				FD_SET(c->right, r);
			} else {
				FD_SET(c->right, w);
			}
			if (c->right > maxfd) maxfd = c->right;
		}
	}
	return maxfd;
}
static void accept_cpipe(struct cpipe *c)
{
	struct sockaddr_in sin;
	int fd, r, j;

	j = sizeof(sin);
	r = accept(c->left, (struct sockaddr*)&sin, &j);
	if (r == -1) return;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		(void)close(r);
		return;
	}
	sockop_nonblock(fd, 1);
	sockop_keepalive(fd, 1);
	sockop_ndelay(fd, 1);

	sockop_nonblock(r, 1);
	sockop_keepalive(r, 1);
	sockop_ndelay(r, 1);
	if (connect(fd, (struct sockaddr *)&c->todo, sizeof(c->todo)) == 0) {
		/* immediately successful! */
		(void)close(c->left);
		c->left = r;
		c->right = fd;
		c->state = 1;
		sockop_nonblock(fd, 0);
		sockop_nonblock(r, 0);
		return;
	}

#ifdef EINPROGRESS
	if (errno != EINPROGRESS)
#endif
#ifdef EWOULDBLOCK
		if (errno != EWOULDBLOCK)
#endif
		{
			(void)close(fd);
			return;
		}

	(void)close(c->left);
	c->left = r;
	c->right = fd;
	c->expires = time(0) + BRINGUP_TIMEOUT;
	return;
}

static void update_cpipes(fd_set *r, fd_set *w)
{
	register struct cpipe *c;
	struct sockaddr_in sin;
	int dummy;

	for (c = top; c; c = c->next) {
		if (c->left == -1) continue;
		if (FD_ISSET(c->left, r)) {
			if (c->state == 0) {
				accept_cpipe(c);
			} else if (c->state == 1 || c->state == 3) {
				if (bufferin(c->left)) {
					c->expires = time(0) + CHATTER_TIMEOUT;
					if (!bufferout(c->right)) {
						c->expires = time(0) + CHATTER_TIMEOUT;
						c->state = 2;
						/* hangup */
						(void)shutdown(c->right, SHUT_WR);
						(void)shutdown(c->left, SHUT_RD);
					}
				} else if (c->state == 3) {
					(void)close(c->right);
					(void)close(c->left);
					c->left = c->right = -1;
					c->state = 0;
				} else {
					c->expires = time(0) + CHATTER_TIMEOUT;
					c->state = 2;
					/* hangup */
					(void)shutdown(c->right, SHUT_WR);
					(void)shutdown(c->left, SHUT_RD);
				}
			}
		}
		if (c->right == -1) continue;
		if (c->state == 0) {
			if (FD_ISSET(c->right, w)) {
				dummy = sizeof(sin);
				if (getpeername(c->right,
						(struct sockaddr *)&sin,
						&dummy) == -1) {
					/* failure */
					(void)close(c->right);
					(void)close(c->left);
					c->left = c->right = -1;
				} else {
					/* actually connected! hurrah! */
					c->state = 1;
					sockop_nonblock(c->left, 0);
					sockop_nonblock(c->right, 0);
					c->expires = time(0) + CHATTER_TIMEOUT;
				}
			}

		} else if (FD_ISSET(c->right, r)) {
			if (c->state == 1 || c->state == 2) {
				if (bufferin(c->right)) {
					c->expires = time(0) + CHATTER_TIMEOUT;
					if (!bufferout(c->left)) {
						c->expires = time(0) + CHATTER_TIMEOUT;
						c->state = 3;
						/* hangup */
						(void)shutdown(c->left, SHUT_WR);
						(void)shutdown(c->right, SHUT_RD);
					}
				} else if (c->state == 2) {
					(void)close(c->right);
					(void)close(c->left);
					c->left = c->right = -1;
					c->state = 0;
				} else {
					c->expires = time(0) + CHATTER_TIMEOUT;
					c->state = 3;
					/* hangup */
					(void)shutdown(c->left, SHUT_WR);
					(void)shutdown(c->right, SHUT_RD);
				}
			}
		}
	}
}
static int expire_cpipes(void)
{
	register int min = -1;
	register struct cpipe *c, *cl;
	register int df;
	time_t now;

	time(&now);
	for (c = top, cl = 0; c; c = c->next) {
		df = c->expires - now;
		if (df > 0 && (c->left > -1 || c->right > -1)) {
			if (min == -1 || min > df) min = df;
			cl = c;
		} else {
			/* reap */
			if (cl) {
				cl->next = c->next;
			} else {
				top = c->next;
			}
			if (c->left > -1) (void)close(c->left);
			if (c->right > -1) (void)close(c->right);
			(void)free(c);
		}
	}

	return min;
}
static int lookup_peer(int fd)
{
	register struct cpipe *c;
	for (c = top; c; c = c->next) {
		if (c->left == fd) {
			if (c->state == 0) return -1;
			return c->right;
		}
		if (c->right == fd) {
			if (c->state == 0) return -1;
			return c->left;
		}
	}
	return -1;
}

static char *hasdev(char **xret)
{
	char *x, *y;

	x = *xret;
	y = (char *)strchr(x, ':');
	if (!y) return 0;
	(*xret) = (y+1);
	return x;
}

int main(int argc, char *argv[])
{
	struct cpipe *c;
	unsigned char packet[512];
	struct timeval tv, *ptv;
	struct sockaddr_in sin, sendt, binda;
	struct ip_mreq gamr;
	in_addr_t ina, inb;
	unsigned int bf;
	fd_set rfds, wfds;
	char *q, *p;
	int bfd, sfd, maxfd, fd;
	int i, r;

	if (argc != 3 && argc != 2) {
#define M "Usage: cproxy bindout [sendto]\n"
		for (i = 0; i < sizeof(M)-1;) {
			do {
				r = write(2, M+i,(sizeof(M)-1)-i);
			} while (r == -1 && errno == EINTR);
			if (r < 1) exit(255);
			i += r;
		}
		exit(0);
#undef M
	}

	bfd = socket(AF_INET, SOCK_DGRAM, 0);
	sfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (bfd == -1 || sfd == -1) exit(111);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;

	q = (char *)getenv("GROUP");
	if (!q) q = "255.255.255.255";
	if ((p = hasdev(&q))) {
#ifdef SO_BINDTODEVICE
		(void)setsockopt(bfd, SOL_SOCKET, SO_BINDTODEVICE,
				 p, strlen(p));
#endif
	}
	
	ina = inet_addr(q);
	memcpy(&sin.sin_addr, &ina, sizeof(in_addr_t));
	i = *((unsigned char *)&ina);
	if (i >= 224 && i < 255) {
		/* multicast */
		i = 1; (void)setsockopt(bfd, IPPROTO_IP, IP_MULTICAST_LOOP, &i, sizeof(i));
		i = 31; (void)setsockopt(bfd, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i));
	}
	sockop_broadcast(bfd, 1);
	sockop_reuseaddr(bfd, 1);
	sin.sin_port = htons(CLUSTER_PORT);
	if (bind(bfd, (struct sockaddr *)&sin, sizeof(sin)) == -1) exit(111);

	/* sending for broadcasts... */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;

	inb = inet_addr(argv[1]);
#ifndef INADDR_NONE
#define INADDR_NONE	((in_addr_t)-1)
#endif
	if (inb == INADDR_NONE) exit(111);
	memcpy(&sin.sin_addr, &inb, sizeof(in_addr_t));
	r = *((unsigned char *)&inb);
	if (r >= 224 && r < 255) exit(255); /* can't fucking bind multicast */

	q = (argc == 2 ? "255.255.255.255" : argv[2]);
	if ((p = hasdev(&q))) {
#ifdef SO_BINDTODEVICE
		(void)setsockopt(sfd, SOL_SOCKET, SO_BINDTODEVICE,
				 p, strlen(p));
#endif
	}

	ina = inet_addr(q);
	i = *((unsigned char *)&ina);
	if (i >= 224 && i < 255) {
		/* multicast */
		i = 1; (void)setsockopt(sfd, IPPROTO_IP, IP_MULTICAST_LOOP, &i, sizeof(i));
		i = 31; (void)setsockopt(sfd, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i));

		memset(&gamr, 0, sizeof(gamr));
		memcpy(&gamr.imr_multiaddr, &inb, sizeof(in_addr_t));
		memcpy(&gamr.imr_interface, &ina, sizeof(in_addr_t));
		/* join the group */
		(void)setsockopt(sfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
					 &gamr,sizeof(gamr));
	}
	sockop_broadcast(sfd, 1);
	sockop_reuseaddr(sfd, 1);
	sin.sin_port = htons(0); /* send from any port */
	memcpy(&binda, &sin, sizeof(sin));
	if (bind(sfd, (struct sockaddr *)&sin, sizeof(sin)) == -1) exit(111);

	/* ... and connect to */
	memset(&sendt, 0, sizeof(sendt));
	sendt.sin_family = AF_INET;
	memcpy(&sendt.sin_addr, &ina, sizeof(in_addr_t));
	sendt.sin_port = htons(CLUSTER_PORT);


	for (fd = -1;;) {
		if (fd == -1) {
			fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd > -1) {
				memcpy(&sin, &binda, sizeof(sin));
				if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
					close(fd);
					fd = -1;
				}
			}
		}

		do {
			tv.tv_sec = expire_cpipes();
			tv.tv_usec = 0;

			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
			FD_SET(bfd, &rfds);
			maxfd = select_cpipes(bfd, &rfds, &wfds);

			ptv = (tv.tv_sec < 0)
				? ((struct timeval *)0)
				: ((struct timeval *)&tv);
			r = select(maxfd+1, &rfds, &wfds, (fd_set *)0, ptv);
		} while (r == -1 && errno == EINTR);
		if (r < 0) {
			exit(250);
		}
		if (r == 0) continue;

		update_cpipes(&rfds, &wfds);

		if (fd != -1 && !FD_ISSET(bfd, &rfds)) continue;

		i = sizeof(sin);
		r = recvfrom(bfd, packet, sizeof(packet), 0,
				(struct sockaddr *)&sin, &i);
		if (r < CLUSTER_SYNC+4) continue;
		if (memcmp(&sin.sin_addr, &inb, sizeof(inb)) == 0) continue;
		if (memcmp(&sin.sin_addr, &ina, sizeof(ina)) == 0) continue;


		/* copy port number for copy back */
		memcpy(&sin.sin_port, packet+CLUSTER_SYNC, sizeof(sin.sin_port));
		if (!(c = add_cpipe(fd, &sin))) continue;
		if (fd != c->left) {
			/* add_cpipe changed fd; close old one */
			(void)close(fd);
			fd = c->left;
			/* already listening... */
		} else {
			if (listen(fd, 1) == -1) {
				(void)close(fd);
				fd = c->left = c->right = -1;
				continue;
			}
		}

		i = sizeof(sin);
		if (getsockname(fd, (struct sockaddr *)&sin, &i) == -1) {
			(void)close(fd);
			fd = c->left = c->right = -1;
			continue;
		}

		memcpy(packet+CLUSTER_SYNC, &sin.sin_port, sizeof(sin.sin_port));
		if (sendto(sfd, packet, r, 0,
					(struct sockaddr *)&sendt,
					sizeof(sendt)) == -1) {
			(void)close(fd);
			fd = c->left = c->right = -1;
		}
		if (fd > -1) {
			sockop_nonblock(fd, 1);
			sockop_keepalive(fd, 1);
			sockop_ndelay(fd, 1);
			fd = -1;
		}
	}
}

