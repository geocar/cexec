#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "cluster.h"

#include "bio.h"
struct bio biodata;
struct bio copyout;

#include "ec_crypt.h"
#include "ec_vlong.h"
static vl_point secret_key;
static vl_point public_key;

/* load.c */
int system_load(struct timeval *tv);

unsigned int autorat;

static int selfpipe[2];
static void selfpipe_write(int signo)
{
	signal(SIGCHLD, selfpipe_write);
	if (selfpipe[1] > -1) (void) write(selfpipe[1], "\x01", 1);
}
static void timeout_die(int signo)
{
	exit(127);
}

static void addenv_ip(const char *base, int baselen, struct sockaddr_in *s)
{
	char buffer[4096];
	char *p;
	int j;

	memcpy(buffer, base, baselen);
	memcpy(buffer+baselen, "ADDR=", 5);
	j = ip_print(buffer+baselen+5, s->sin_addr.s_addr);
	buffer[baselen+5+j] = 0;
	p = (char *)strdup(buffer);
	if (!p) return;
	if (putenv(p) == -1) return;

	memcpy(buffer, base, baselen);
	memcpy(buffer+baselen, "PORT=", 5);
	j = number_print(buffer+baselen+5, ntohs(s->sin_port));
	buffer[baselen+5+j] = 0;
	p = (char *)strdup(buffer);
	if (!p) return;
	if (putenv(p) == -1) return;
}
static int doit(int nfd, int fd, unsigned char *msg, int msglen, char **argvp)
{
	struct timeval tv;
	unsigned char header[4], *q, *end, *tmp;
	unsigned int byteorder;
	int flip_bits;
	struct sockaddr_in sin;
	fd_set rfds, use;
	fd_set mask_in, mask_out;
	int *fdmap, *svmap, sv[2];
	unsigned int len, subfd;
	vl_point s1, s2, ix;
	cp_pair sig;
	pid_t child, p;
	int i, r;
	int fds, nm;
	int stillopen;
	int status;
	int use_mask;
	int may_die;
	int no_read;
	int ok;

	switch ((p = fork())) {
	case -1: return 0;
	case 0: break; /* child goes that way */
	default: return 1;
	};

	signal(SIGPIPE, SIG_IGN);

	(void) close(nfd);

	if (!bio_setup_malloc(&biodata, PIPE_BUF)) exit(100);
	if (!bio_setup_malloc(&copyout, PIPE_BUF)) exit(101);

	(void)close(selfpipe[0]);
	(void)close(selfpipe[1]);
	selfpipe[0] = selfpipe[1] = -1;
	
	/*
	 * msg[0..CLUSTER_SYNC]			message sync code (in T/TCP)
	 * CLUSTER_SYNC+0,CLUSTER_SYNC+1	port number
	 * CLUSTER_SYNC+2,CLUSTER_SYNC+3	file descriptors
	 * CLUSTER_SYNC+4...			envp
	 */

	fds = get_uint16(msg+CLUSTER_SYNC+2);
	if (fd >= 0xFF00) exit(113);

	fdmap = (int *)malloc(fds * sizeof(int));
	svmap = (int *)malloc(fds * sizeof(int));
	if (!fdmap || !svmap) exit(127);

	hash_point(s1, s2, msg, CLUSTER_SYNC);
	vl_clear(ix);
	vl_shortset(ix, 1);
	do {
		cp_sign(secret_key, s1, s2, &sig);
		vl_add(s1, ix);
	} while (sig.r[0] == 0 || sig.s[0] == 0);

	/* 30 seconds to write */
	signal(SIGALRM, timeout_die);
	alarm(30);

	errno = 0;
	if (!vlpoint_write(sig.s, fd) || !vlpoint_write(sig.r, fd)) {
		exit(102);
	}
	if (!cread(fd, header, 4)) {
		exit(102);
	}
	alarm(0);

	memcpy(&byteorder, header, 4);

	FD_ZERO(&mask_in);
	FD_ZERO(&mask_out);
	q = msg+CLUSTER_SYNC+8;
	end = msg + msglen;
	use_mask = 0;
	while (q < end && q >= msg+CLUSTER_SYNC+8) {
		if (memcmp(q, "CEXEC FD BITMAP=", 16) == 0) {
			q += 16;
			if ((*q & 64) && !(*q & 128)
			&& (*q & 1) && !(*q & 2)) {
				/* odd, bit order backwards? */
				flip_bits = 1;
			} else {
				flip_bits = 0;
			}
			for (i = 0; i < fds; i += 3) {
				if (flip_bits) {
					r = *q;
					*q = ((r & 1) << 7)
					| ((r & 2) << 5)
					| ((r & 4) << 3)
					| ((r & 8) << 1)
					| ((r & 16) >> 1)
					| ((r & 32) >> 3)
					| ((r & 64) >> 5)
					| ((r & 128) >> 7);
				}
				if (*q & 64) break;
				if (!(*q & 128)) break;

				if (*q & 1) FD_SET(i, &mask_in);
				if (*q & 2) FD_SET(i, &mask_out);

				if (*q & 4) FD_SET(i+1, &mask_in);
				if (*q & 8) FD_SET(i+1, &mask_out);

				if (*q & 16) FD_SET(i+2, &mask_in);
				if (*q & 32) FD_SET(i+2, &mask_out);
				q++;
			}
			if (i >= fds) use_mask = 1;
			break;
		}
		q = (char*)strchr(q, '\0')+1;
	}

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);nm=fd;
	for (i = stillopen = 0; i < fds; i++) {
		no_read = 0;
		if (use_mask) {
			if (FD_ISSET(i, &mask_out) && FD_ISSET(i, &mask_in)) {
				if (pair_duplex(sv) == -1) exit(127);
			} else if (FD_ISSET(i, &mask_out)) {
				if (pair_simplex(sv) == -1) exit(127);
			} else if (FD_ISSET(i, &mask_in)) {
				if (pair_simplex(sv) == -1) exit(127);
				no_read = 1;
				r = sv[0];
				sv[0] = sv[1];
				sv[1] = r;
			} else {
				fdmap[i] = -1;
				svmap[i] = -1;
				continue;
			}
		} else {
			if (pair_duplex(sv) == -1) exit(127);
		}

		fdmap[i] = sv[0];
		svmap[i] = sv[1];

		if (!no_read) {
			FD_SET(sv[0], &rfds);
			if (sv[0] > nm) nm=sv[0];
		}
		stillopen++;
	}
	if (pipe(selfpipe) == -1) exit(103);
	sockop_nonblock(selfpipe[0],1);
	sockop_nonblock(selfpipe[1],1);
	FD_SET(selfpipe[0], &rfds);
	if (selfpipe[0] > nm) nm = selfpipe[0];
	signal(SIGCHLD, selfpipe_write);

	switch ((child = fork())) {
	case -1: exit(127);
	case 0:
		/* load extra environment */
		q = msg+CLUSTER_SYNC+8;
		end = msg + msglen;
		while (q < end) {
			q[-5] = 'P';
			q[-4] = 'E';
			q[-3] = 'E';
			q[-2] = 'R';
			q[-1] = '_';
			for (i = ok = 0; q[i] && q+i < end; i++) {
				if (q[i] == ' ' && !ok) ok=-1;
				else if (q[i] == '=' && !ok) ok++;
			}
			if (!q[i] && ok > 1) {
				if (!i) break;
				tmp = (char*)strdup(q-5);
				if (tmp) putenv(tmp);
			}
			q += (i+1);
		}

		/* add REMOTE_ADDR, REMOTE_PORT, etc */
		i = sizeof(sin);
		if (getsockname(fd, (struct sockaddr *)&sin, &i) != -1)
			addenv_ip("SERVER_", 7, &sin);
		i = sizeof(sin);
		if (getpeername(fd, (struct sockaddr *)&sin, &i) != -1)
			addenv_ip("REMOTE_", 7, &sin);

		/* putenv? */
		(void)close(fd);
		(void)close(selfpipe[0]);
		(void)close(selfpipe[1]);
		for (i = 0; i < fds; i++) {
			(void)close(i);
			if (svmap[i] == -1) continue;

			if (dup2(svmap[i], i) == -1) exit(127);
			(void)close(svmap[i]);
			(void)close(fdmap[i]);
			if (fcntl(i, F_SETFD, 0) == -1) exit(127);
		}
		execvp(*argvp,argvp);
		exit(127);
	};

	/* okay, in sane state now... */
	for (i = 0; i < fds; i++) {
		(void)close(svmap[i]);
	}
	free(svmap); /* maybe libc can do something with this... */

	may_die = 0;
	for (status = -1; stillopen > 0;) {
		fdset_copy(nm+1,&rfds,&use);
		do {
			tv.tv_sec = 600;
			tv.tv_usec = 0;
			r = select(nm+1,&use,(fd_set*)0,(fd_set*)0,
					(struct timeval *)&tv);
		} while (r == -1 && errno == EINTR);
		if (r == 0) {
			(void)kill(child, SIGTERM);
			exit(104);
		}
		if (FD_ISSET(fd, &use)) {
			do {
				/* demultiplex input */
				if (!bio_need(fd,&biodata,4)) {
					(void)kill(child, SIGTERM);
					exit(109);
				}
				if (!bio_headin(&biodata,header,4)) {
					(void)kill(child, SIGTERM);
					exit(106);
				}
				len = get_uint16(header);
				subfd = get_uint16(header+2);

				/* ping? */
				if (subfd == 0xFFFF && len == 0xFFFF) {
					/* pong */
					if (status == -1 && kill(child, 0) == -1) {
						may_die = 1;
					}
					bio_skipin(&copyout, 4);
					put_uint16(header, 0xFFFF);
					put_uint16(header+2, 0xFFFF);
					bio_headout(&copyout, header, 4);
					if (!bio_blast(fd, &copyout, 0, 0))
						exit(110);
					bio_base(&copyout);
					continue;
				}

				if (subfd == 0) {
					(void)kill(child, SIGTERM);
					exit(107);
				}
				subfd--; /* now an fd in map */
				if (subfd >= fds) {
					/* damnit */
					(void)kill(child, SIGTERM);
					exit(108);
				}

				/* close signal */
				if (len == 0) {
					if (fdmap[subfd] > -1 && fdmap[subfd] != fd) {
						FD_CLR(fdmap[subfd], &rfds);
						FD_CLR(fdmap[subfd], &use);
						(void)close(fdmap[subfd]);
						fdmap[subfd] = -1;
						stillopen--;

						/* send close alert */
						bio_skipin(&copyout, 4);
						put_uint16(header, 0);
						put_uint16(header+2, subfd+1);
						bio_headout(&copyout, header, 4);
						if (!bio_blast(fd, &copyout, 0, 0)) {
							(void)kill(child, SIGTERM);
							exit(110);
						}
						bio_base(&copyout);
					}
				} else {

					if (!bio_copy(fd, fdmap[subfd],
					&biodata, 4, len) && fdmap[subfd] != fd) {
						/* closed on this end;
						 * will notice momentarilly
						 */
						FD_CLR(fdmap[subfd], &rfds);
						FD_CLR(fdmap[subfd], &use);
						(void)close(fdmap[subfd]);
#if 0
						fdmap[subfd] = -1;
						stillopen--;

						/* send close alert */
						bio_skipin(&copyout, 4);
						put_uint16(header, 0);
						put_uint16(header+2, subfd+1);
						bio_headout(&copyout, header, 4);
						if (!bio_blast(fd, &copyout, 0, 0)) {
							(void)kill(child, SIGTERM);
							exit(110);
						}
						bio_base(&copyout);
#endif
					}
				}
			} while (bio_base(&biodata));

			FD_CLR(fd, &use); /* in case fd < maxfd */
		}
		for (i = 0; i < fds; i++) {
			if (i == fd) continue;
			if (fdmap[i] == -1) continue;
			if (!FD_ISSET(fdmap[i], &use)) continue;
			bio_skipin(&copyout, 4);
			if (!bio_more(fdmap[i], &copyout)) {
				/* okay, channel closed.
				 * send to peer
				 */
				FD_CLR(fdmap[i], &rfds);
				(void)close(fdmap[i]);
				stillopen--;
				fdmap[i] = -1;

				/* close command */
				put_uint16(header, 0);
			} else {
				p = bio_ready(&copyout) - 4;
				if (!p) continue;

				put_uint16(header, p);
			}

			put_uint16(header+2, i+1);
			bio_headout(&copyout, header, 4);
			if (!bio_blast(fd, &copyout, 0, 0)) {
				(void)kill(child, SIGTERM);
				exit(110);
			}
			bio_base(&copyout);
		}

		if (FD_ISSET(selfpipe[0], &use)) {
			(void)read(selfpipe[0], header, sizeof(header));
		}
		while ((p = waitpid(child, &r, WNOHANG)) > 0) {
			if (p != child) continue;
			/* alright, got status code
			 */
			if (WIFEXITED(r)) {
				status = WEXITSTATUS(r);
			} else {
				exit(255);
			}
		}
		if (status == -1 && may_die) {
			/* okay, child died... */

			exit(255);
		}
	}
	if (status == -1) {
		while (waitpid(child, &r, 0) < 1);
		if (WIFEXITED(r)) {
			status = WEXITSTATUS(r);
		} else {
			exit(255);
		}
	}
	/* cool, send status back */
	bio_skipin(&copyout, 4);
	put_uint16(header, status);
	put_uint16(header+2, 0);
	bio_headout(&copyout, header, 4);
	if (!bio_blast(fd, &copyout, 0, 0)){
		exit(112);
	}
	bio_base(&copyout);
	exit(0);
}

int isiplocal(unsigned char *ip)
{
	struct sockaddr_in sin;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) return 1; /* fuck... */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(0);
	memcpy(&sin.sin_addr, ip, 4);
	if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		(void)close(fd);
		return 0;
	}
	(void)close(fd);
	return 1;
}


int main(int argc, char *argv[])
{
	struct ip_mreq gamr;
	unsigned int pubhash;
	unsigned char buf[512];
	unsigned char rattest[9];
	unsigned char *x;
	char *q, *hashbuf;
	struct timeval tv;
	struct sockaddr_in sin;
	unsigned int hashbuf_size;
	int children, maxchildren;
	in_addr_t ina;
	fd_set rfds, wfds;
	int fd, nfd, r, i;
	int need_delay;
	int approx_seconds;
	struct timeval tv_boost;
	int use_boost;
	pid_t p;

	gf_init();

	if (argc < 3) {
#define M "Usage: cservice app exec...\n"
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

	autorat = 0;

	i = open(argv[1], O_RDONLY);
	if (i == -1) exit(111);
	if (!vlpoint_read(secret_key, i)) exit(111);
	(void)close(i);

	vl_clear(public_key);
	cp_makepublickey(public_key, secret_key);
	hashbuf_size = vlpoint_size(public_key);
	hashbuf = malloc(hashbuf_size + CLUSTER_SYNC);
	if (!hashbuf) exit(111);
	if (!vlpoint_pack(public_key, hashbuf + CLUSTER_SYNC, hashbuf_size))
		exit(127); /* ? */

	use_boost = 0;
	q = (char *)getenv("BOOST");
	if (q) {
		tv_boost.tv_sec = 0;
		tv_boost.tv_usec = (i = atoi(q));
		if (i > -1) use_boost = 1;
	}

	maxchildren = -1;
	q = (char *)getenv("CONCURRENCY");
	if (q) maxchildren = atoi(q);
	if (maxchildren < 1) maxchildren = 50;


	if (pipe(selfpipe) == -1) exit(111);
	for (i = 0; i < 2; i++) {
		(void) fcntl(selfpipe[i], F_SETFL,
			     fcntl(selfpipe[i], F_GETFL, 0) | O_NONBLOCK);
	}
	signal(SIGCHLD, selfpipe_write);
	signal(SIGPIPE, SIG_IGN);

	nfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (nfd == -1) exit(111);

	memset(&sin, 0, sizeof(sin));
	q = (char *)getenv("GROUP");
	if (!q || !*q) q = "255.255.255.255";
	ina = inet_addr(q);
	memcpy(&sin.sin_addr, &ina, sizeof(in_addr_t));

	i = *((unsigned char *)&ina);
	if (i >= 224 && i < 255) {
		/* multicast */
		memset(&gamr, 0, sizeof(gamr));
		memcpy(&gamr.imr_multiaddr, &ina, sizeof(in_addr_t));
		i = 1; (void)setsockopt(nfd, IPPROTO_IP, IP_MULTICAST_LOOP, &i, sizeof(i));
		i = 31; (void)setsockopt(nfd, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i));
		/* we might already have joined... */
		i=1;
		(void)setsockopt(nfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				 &gamr,sizeof(gamr));
	}
	sockop_broadcast(nfd);
	sockop_reuseaddr(nfd);
	sin.sin_port = htons(CLUSTER_PORT);
	if (bind(nfd, (struct sockaddr *)&sin, sizeof(sin)) == -1) exit(111);
	sockop_nonblock(nfd);

	for (children = 0, need_delay = 1;;) {
		FD_ZERO(&rfds);
		FD_SET(selfpipe[0], &rfds);
		if (children < maxchildren) FD_SET(nfd, &rfds);
		do {
			r = select((selfpipe[0] < nfd
						? nfd+1 : selfpipe[0]+1),
					&rfds, (fd_set *)0, (fd_set *)0,
					(struct timeval *)0);
		} while (r == -1 && errno == EINTR);

		if (FD_ISSET(selfpipe[0], &rfds)) {
			(void)read(selfpipe[0], buf, sizeof(buf));
		}

		while ((p = waitpid(-1, &r, WNOHANG)) > 0) {
			children--;
		}

		if (FD_ISSET(nfd, &rfds) && children < maxchildren) {

			if (need_delay) {
				do {
					if (use_boost) {
						memcpy(&tv, &tv_boost, sizeof(tv));
					} else if (!system_load(&tv)) {
						tv.tv_sec = 0;
						tv.tv_usec = (children*1000)
							/ maxchildren;
					}
					tv.tv_sec += autorat;
					i = select(0, (fd_set *)0,
							(fd_set *)0,
							(fd_set *)0,
							&tv);
				} while (i != 0);
				need_delay = 0;
				if (autorat) autorat--;
			}

			for (;;) {
				/* got a broadcast message, read the datagram */
				i = sizeof(struct sockaddr_in);
				r = recvfrom(nfd, buf, sizeof(buf), 0,
						(struct sockaddr *)&sin, &i);
				if (r < 1) break;

				if (r == 9 || r == 8) {
					if (!isiplocal(buf))
						continue;

					/* okay, this is a ratout message.
					 * if it refers to us, we're going
					 * to add artifical load until we
					 * recover....
					 */
					memcpy(rattest, buf, 4);
					put_uint32(rattest+4,
				hash(hashbuf + CLUSTER_SYNC, hashbuf_size));
					put_uint32(rattest+4, hash(rattest, 8));
					if (memcmp(rattest, buf, 8) != 0)
						continue;

					/* okay, they're ratting this service */
					if (r == 8 && autorat == 0) {
						autorat = 2;
					} else if (r == 9 && autorat > 0) {
						autorat = 0;
					}

					/* go back to delay */
					break;
				}

				i = CLUSTER_SYNC + sizeof(sin.sin_port) + 2;
				x = buf + i;
				if (r < i) continue;

				memcpy(hashbuf, buf, CLUSTER_SYNC);
				pubhash = hash(hashbuf, hashbuf_size+CLUSTER_SYNC);
				if (pubhash != get_uint32(x)) continue;

				/* okay, attempt to connect */
				fd = socket(AF_INET, SOCK_STREAM, 0);
				if (fd == -1) continue;

				sockop_keepalive(fd, 1);
				sockop_ndelay(fd, 0);

				memcpy(&sin.sin_port, buf+CLUSTER_SYNC,
						sizeof(sin.sin_port));
				sin.sin_family = AF_INET;
				sockop_nonblock(fd, 1);

				if (connect(fd, (struct sockaddr *)&sin,
							sizeof(sin)) == 0) {
					/* whoah! that was quick! */
					if (doit(nfd, fd, buf, r, argv+2)) {
						children++;
						need_delay = 1;
					}
					(void)close(fd);
					break;
				}

#ifdef EINPROGRESS
				if (errno != EINPROGRESS)
#endif
#ifdef EWOULDBLOCK
					if (errno != EWOULDBLOCK)
#endif
				{ (void)close(fd); break; }

				/* ten second limit */
				tv.tv_sec = 10;
				tv.tv_usec = 0;
				FD_ZERO(&wfds);
				FD_SET(fd, &wfds);
				do {
					i = select(fd+1, (fd_set *)0,
							&wfds,
							(fd_set *)0,
							&tv);
				} while (i == -1 && errno == EINTR);

				sockop_nonblock(fd, 0);

				i = sizeof(sin);
				if (getpeername(fd, (struct sockaddr *)&sin,
							&i) == 0) {
					if (doit(nfd, fd, buf, r, argv+2)) {
						children++;
						need_delay = 1;
					}
					(void)close(fd);
					break;
				}

				/* grr.... wee, we tried */
				(void)close(fd);
				break;
			}
		}
	}
}
