#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include "cluster.h"
#include "netnum.h"

void parity_init(void);
int parity(void);

#include "bio.h"
struct bio biodata;
struct bio copyout;

#include "ec_crypt.h"
#include "ec_vlong.h"

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif

/* bottom 4 bytes are the host to rat out,
 * the top four are H(host,H(pubkey))
 *
 * this is a special message that's always
 * EXACTLY 8 characters long
 */
#include "strline.h"

static unsigned int timeout_accept = 60;
static unsigned int timeout_pingpong = 600;
static int maxfd;


static unsigned char rathost[9];
static void ratout(int bfd, in_addr_t group, int ee)
{
	struct sockaddr_in sin;
	int fdout;

	if (getenv("CEXEC_FD_FOR_ERRORS")) {
		fdout = atoi(getenv("CEXEC_FD_FOR_ERRORS"));
		if (fdout > -1 && (fdout > maxfd || maxfd == -1)) {
			char outline[1024];
			unsigned char *op;
			unsigned int ip;
			unsigned int i;
			unsigned int len;
			int r;

			ip = rathost[0]
			| (rathost[1] << 8)
			| (rathost[2] << 16)
			| (rathost[3] << 24);

			op = outline;

			if (!ip) {
				op += static_str(op, "[cluster]");
			} else {
				op += ip_print(op, ip);
			}
			if (ee == -1 || ee == -5) {
				op += static_str(op, " failed to respond after ");
				if (ee == -1) {
					op += number_print(op, timeout_pingpong);
				} else {
					op += number_print(op, timeout_accept);
				}
				op += static_str(op, " seconds\n");
			} else if (ee == -2) {
				op += static_str(op, " disconnected unexpectedly\n");
			} else if (ee == -3) {
				op += static_str(op, " violated protocol\n");
			} else if (ee == -4) {
				op += static_str(op, " test alert\n");
			} else if (ee == -6) {
				op += static_str(op, " could not read signature\n");
			} else if (ee == -7) {
				op += static_str(op, " signature didn't verify\n");
			} else if (ee < 0) {
				op += static_str(op, " unknown error\n");
			} else {
				op += static_str(op, " exited with code ");
				op += number_print(op, (unsigned int)ee);
				op += static_str(op, "\n");
			}

			len = op - ((unsigned char *)outline);
			for (i = 0; i < len;) {
				do {
					r = write(fdout, outline+i, len-i);
				} while (r == -1 && errno == EINTR);
				if (r < 1) break; /* already error cond. */
				i += r;
			}
		}
	}

	/* attempt to rat out rathost */
	if (rathost[0] || rathost[1] || rathost[2] || rathost[3]) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(CLUSTER_PORT);
		memcpy(&sin.sin_addr, &group, sizeof(group));
		if (ee < 0) {
			(void)sendto(bfd, rathost, 8, 0,
				      (struct sockaddr *)&sin, sizeof(sin));
		} else {
			rathost[8] = (ee & 0xFF);
			(void)sendto(bfd, rathost, 9, 0,
				      (struct sockaddr *)&sin, sizeof(sin));
		}
	}

	if (ee < 0) {
		exit(111);
	}
	exit(ee);
}

static int try_bind(int portno)
{
	struct sockaddr_in sin;
	int i, sfd;

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1) return -1;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;

	sin.sin_port = htons(portno);
	sockop_keepalive(sfd, 1);
	sockop_ndelay(sfd, 0);
	sockop_reuseaddr(sfd, 1);
	if (bind(sfd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		(void)close(sfd);
		return -1;
	}
	if (listen(sfd, 1) == -1) {
		(void)close(sfd);
		return -1;
	}
	return sfd;
}


int main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	unsigned char header[4];
	unsigned int bohack;
	unsigned char rbuf[512], *rp, *hp, *pp;
	unsigned char cbuf[CLUSTER_SYNC];
	unsigned int len, subfd;
	unsigned char *hashbuf;
	unsigned int hashbuf_size;
	int start_port, end_port;
	in_addr_t ina, thost;
	int use_thost;
	int i, j, r, bfd, sfd, fd, nm;
	struct timeval tv;
	time_t now, start;
	fd_set rfds, use;
	fd_set lr, lw;
	vl_point pub, s1, s2;
	cp_pair sig;
	int skip_send;
	char *avoidh;
	int avoidc;
	char *q, *p;

	maxfd = -1;
RESTART:
	if (maxfd > -1) {
		for (i = 0; i <= maxfd; i++) {
			(void)lseek(i, 0, SEEK_SET);
		}
	}

	gf_init();
	memset(rathost, 0, sizeof(rathost));
	signal(SIGPIPE, SIG_IGN);

	if (argc < 2) {
#define M "Usage: cexec app [env...]\n"
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

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		q = (char*)malloc(strlen(argv[1])+5);
		if (!q) exit(111);
		strcpy(q, argv[1]);
		strcat(q, ".pub");
		fd = open(q, O_RDONLY);
		if (fd == -1) exit(111);
		(void)free(q);
	}
	if (!vlpoint_read(pub, fd)) exit(111);
	(void)close(fd);

	if (!bio_setup_malloc(&biodata, PIPE_BUF)) exit(111);
	if (!bio_setup_malloc(&copyout, PIPE_BUF)) exit(111);

	q = (char *)getenv("CEXEC_TIMEOUT_ACCEPT");
	if (q) timeout_accept = atoi(q);
	if (timeout_accept < 0) timeout_accept = 0;

	/* shouldn't set this... */
	q = (char *)getenv("CEXEC_TIMEOUT_PINGPONG");
	if (q) timeout_pingpong = atoi(q);
	if (timeout_pingpong < 120) timeout_pingpong = 120;

	maxfd = -1;
	q = (char *)getenv("CEXEC_MAXFD");
	if (!q) q = (char *)getenv("MAXFD");
	if (q) maxfd = atoi(q);
	if (maxfd < 0) maxfd = 32;
	maxfd = fdmap(maxfd+1, &lr, &lw);
	if (maxfd >= 0xFF00) exit(111); /* not allowed */

	parity_init();

	bfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (bfd == -1) exit(111);

	q = (char *)getenv("CEXEC_GROUP");
	if (!q) q = (char *)getenv("GROUP");
	if (!q) q = "255.255.255.255";
	ina = inet_addr(q);

	i = *((unsigned char *)&ina);
	if (i >= 224 && i < 255) {
		/* multicast- but don't subscribe */
		i = 1; (void)setsockopt(bfd, IPPROTO_IP, IP_MULTICAST_LOOP, &i, sizeof(i));
		i = 31; (void)setsockopt(bfd, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i));
	}
	sockop_broadcast(bfd, 1);
	sockop_reuseaddr(bfd, 1);

	/* quorom */
	q = (char *)getenv("CEXEC_REJECT_SERVER");
	if (!q) q = (char *)getenv("CEXEC_REJECT_SERVERS");
	avoidc = 0;
	if (q) {
		avoidh = (char*)malloc(strlen(q)+1);
		while (q) {
			p = strchr(q, ' ');
			if (p) *p = 0;
			thost = inet_addr(q);
			if (p) { *p=' '; q = p + 1; } else q = 0;
			memcpy(avoidh+(avoidc*4), &thost, 4);
			avoidc++;
		}
	} else {
		avoidh = 0;
	}

	/* good for testing... */
	q = (char *)getenv("CEXEC_ACCEPT_SERVER");
	if (q) {
		thost = inet_addr(q);
		use_thost = 1;
		timeout_accept = 0;
	} else {
		use_thost = 0;
	}

	q = (char *)getenv("CEXEC_LISTEN_PORTS");
	if (!q) q = (char *)getenv("CEXEC_LISTEN_PORT");
	if (!q) q = (char *)getenv("CEXEC_PORTS");
	if (!q) q = (char *)getenv("CEXEC_PORT");
	if (!q) {
		sfd = try_bind(0);
	} else {
		for (sfd = -1; sfd == -1;) {
			for (p = q; isdigit(((unsigned int)(*p))); p++);
			j = (unsigned char)*p;
			*p = 0; p++;
	
			start_port = atoi(q);
			if (start_port == -1) break;

			q = p;
			if (j == ':' || j == '-') {
				while (*q == ':' || *q == '-') q++;
				for (p = q; isdigit(((unsigned int)(*p))); p++);
				j = (unsigned char)*p;
				*p = 0; p++;
				end_port = atoi(q);
				if (end_port == -1) {
					sfd = try_bind(start_port);
					break;
				}

				while (start_port <= end_port) {
					sfd = try_bind(start_port);
					if (sfd != -1) break;
					start_port++;
				}
				q = p;
			}
			if (j == 0) {
				sfd = try_bind(start_port);
				break;
			}
			while (*q && !isdigit(((unsigned int)*q))) q++;
			if (!*q) break;
		}
	}
	if (sfd == -1) exit(111);

	i = sizeof(sin);
	if (getsockname(sfd, (struct sockaddr *)&sin, &i) == -1) exit(111);

	rp = rbuf + CLUSTER_SYNC;
	memcpy(rp, &sin.sin_port, sizeof(sin.sin_port));
	rp += sizeof(sin.sin_port);

	/* let the server know how many file descriptors we'll want */
	put_uint16(rp, maxfd+1); rp += 2;

	/* put hash of public key */
	hashbuf_size = vlpoint_size(pub);
	hashbuf = (char *)malloc(hashbuf_size+CLUSTER_SYNC);
	if (!hashbuf) exit(111);
	vlpoint_pack(pub, hashbuf+CLUSTER_SYNC, hashbuf_size);
	hp = rp; rp += 4;
	
	for (i = 2; i < argc; i++) {
		j = strlen(argv[i])+1;
		if (j+rp > rbuf+sizeof(rbuf)-2) exit(111);
		memcpy(rp, argv[i], j);
		rp += j;
	}
	if (rp + ((maxfd * 4) / 6) + 18 < rbuf+sizeof(rbuf)-2) {
		/* eh, it was hopeful */
		memcpy(rp, "CEXEC FD BITMAP=", 16);
		rp += 16;
		*rp = 0x80;
		for (i = j = 0; i <= maxfd; i++) {
			r = 0;
			if (FD_ISSET(i, &lr))
				r |= 1;
			if (FD_ISSET(i, &lw))
				r |= 2;
			*rp |= (r << j);
			j += 2;
			if (j > 5) {
				rp++;
				*rp = 0x80;
				j = 0;
			}
		}
		if (j != 0) rp++;
		*rp++ = 0; /* end of bitmap */

	}
	*rp++ = 0; /* null terminate strings */

	time(&start); memcpy(&now, &start, sizeof(time_t));
	skip_send = 0;

	/* write random challenge */
	for (i = 0, pp = rbuf; i < sizeof(cbuf); i++) {
		do {
			*pp = ((unsigned int)parity())
				| (((unsigned int)parity()) << 1)
				| (((unsigned int)parity()) << 2)
				| (((unsigned int)parity()) << 3)
				| (((unsigned int)parity()) << 4)
				| (((unsigned int)parity()) << 5)
				| (((unsigned int)parity()) << 6)
				| (((unsigned int)parity()) << 7);
		} while (!*pp);
		pp++;
	}

	for (fd = -1; fd == -1 && 
	(timeout_accept == 0 || now-start < timeout_accept); time(&now)) {

		/* update hash */
		memcpy(hashbuf, rbuf, CLUSTER_SYNC);
		put_uint32(hp, hash(hashbuf, hashbuf_size+CLUSTER_SYNC));

		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		memcpy(&sin.sin_addr, &ina, sizeof(in_addr_t));
		sin.sin_port = htons(CLUSTER_PORT);

		for (j = 1, r = 0; r == 0 && j < 32; j++) {
			if (!skip_send && sendto(bfd, rbuf, rp-rbuf, 0,
				      (struct sockaddr *)&sin, sizeof(sin)) == -1) {
				exit(111);
			}
	
			do {
				FD_ZERO(&rfds);
				FD_SET(sfd, &rfds);
				tv.tv_sec = (j >> 1) & 3; tv.tv_usec = 0;
/*				tv.tv_sec = (32-j) & 7; tv.tv_usec = 0; */
				r = select(sfd+1, &rfds, (fd_set *)0,
						(fd_set *)0, &tv);
			} while (r == -1 && errno == EINTR);
		}
		skip_send = 0;
		if (r == 0) continue; /* failed to get picked up */

		i = sizeof(sin);
		fd = accept(sfd, (struct sockaddr *)&sin, &i);
		if (fd == -1) continue;

		if (use_thost) {
			if (memcmp(&sin.sin_addr, &thost, 4) != 0) {
				(void)close(fd);
				skip_send = 1;
				fd = -1;
				continue;
			}
		}
		for (j = 0; j < avoidc; j += 4) {
			if (memcmp(&sin.sin_addr, avoidh+j, 4) != 0) {
				(void)close(fd);
				skip_send = 1;
				fd = -1;
				goto AVOIDED_HOST;
			}
		}

		memcpy(rathost, &sin.sin_addr, 4);
		put_uint32(rathost+4, hash(hashbuf+CLUSTER_SYNC, hashbuf_size));
		put_uint32(rathost+4, hash(rathost, 8));

		sockop_keepalive(fd, 1);
		sockop_ndelay(fd, 0);

		if (!vlpoint_read(sig.s, fd) || !vlpoint_read(sig.r, fd)) {
			(void)close(fd);
			if (use_thost) ratout(bfd, ina, -6);
			skip_send = 1;
			fd = -1;
			continue;
		}

		hash_point(s1, s2, rbuf, CLUSTER_SYNC);
		if (!cp_verify(pub, s2, &sig)) {
			/* failed verification */
			(void)close(fd);
			if (use_thost) ratout(bfd, ina, -7);
			skip_send = 1;
			fd = -1;
			continue;
		}

		bohack = CLUSTER_BYTESEX;
		memcpy(header, (void*)&bohack, 4);
		if (!cwrite(fd, header, 4)) {
			/* can't write? */
			(void)close(fd);
			skip_send = 1;
			fd = -1;
			continue;
		}
AVOIDED_HOST:
		do { /* nothing todo */ } while(0);
	}
	if (fd == -1) {
		ratout(bfd, ina,-5);

	} else if (getenv("CEXEC_RATOUT_TEST")) {
		ratout(bfd, ina,-4);
	}

	/* sin contains the host we're interested in */

	/* we have our worker bee */
	(void)close(sfd);

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds); nm = fd;
	for (i = 0; i <= maxfd; i++) {
		if (FD_ISSET(i, &lr)) FD_SET(i, &rfds);
		if (i > nm) nm = i;
	}

	/* are we still here? */
	time(&start);
	for (;;) {
		fdset_copy(nm+1, &rfds, &use);
		do {
			tv.tv_sec = 5;
			tv.tv_usec = 0;
			r = select(nm+1, &use, (fd_set *)0, (fd_set *)0, &tv);
		} while (r == -1 && errno == EINTR);
		if (r == -1) {
			/* bad fd? maybe? */
			ratout(bfd, ina, -2);
			goto RESTART;
		}
		if (r == 0) {
			time(&now);
			/* 600 seconds with no contact? wow. */
			if (now - start > timeout_pingpong) {
				ratout(bfd,ina,-1);
				goto RESTART;
			}

			/* ping */
			put_uint16(header, 0xFFFF);
			put_uint16(header+2, 0xFFFF);
			bio_headout(&copyout, header, 4);
			if (!bio_blast(fd, &copyout, 0, 0)) {
				ratout(bfd,ina,-2);
				goto RESTART;
			}
			bio_base(&copyout);
			continue;
		}

		/* some activity someplace... */
		time(&start);
		if (FD_ISSET(fd, &use)) {
			do {
				/* demultiplex input */
				if (!bio_need(fd,&biodata,4)
				|| !bio_headin(&biodata, header, 4)) {
					ratout(bfd,ina,-2);
					goto RESTART;
				}
				len = get_uint16(header);
				subfd = get_uint16(header+2);

				if (len == 0xFFFF && subfd == 0xFFFF) {
					/* pong */
					continue;
				}

				if (subfd == 0) {
					/* the END! */
					ratout(bfd,ina,len);
				}
				subfd--; /* now an fd in map */
				if (subfd > maxfd) {
					ratout(bfd,ina,-3);
					goto RESTART;
				}
		
				/* close signal */
				if (len == 0) {
					if (subfd != fd) {
						FD_CLR(subfd, &rfds);
						FD_CLR(subfd, &lr);
						(void)close(subfd);
					}
				} else {
					if (!bio_copy(fd, subfd, &biodata,
								4, len)) {
						/* ack! */
						ratout(bfd,ina,-2);
						goto RESTART;
					}
				}
			} while (bio_base(&biodata));

			FD_CLR(fd, &use); /* in case fd < maxfd */
		}
		for (i = 0; i <= maxfd; i++) {
			if (!FD_ISSET(i, &lr)) continue;
			if (!FD_ISSET(i, &use)) continue;
			bio_skipin(&copyout, 4);
			if (!bio_more(i, &copyout)) {
				/* okay, channel closed.
				 * send to peer
				 */
				FD_CLR(i, &rfds);
				FD_CLR(i, &lr);
				(void)close(i);

				/* close command */
				put_uint16(header, 0);
			} else {
				j = bio_ready(&copyout) - 4;
				if (!j) continue;

				put_uint16(header, j);
			}

			put_uint16(header+2, i+1);
			bio_headout(&copyout, header, 4);
			if (!bio_blast(fd, &copyout, 0, 0)) {
				ratout(bfd,ina,-2);
				goto RESTART;
			}
			bio_base(&copyout);
		}
	}
	exit(127);
}
