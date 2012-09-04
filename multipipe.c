#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static char buffer[32768];
static int bufferx = 0;

static int bufferin(int fd)
{
	int r;

	if (fd == -1) return 0;
	do {
		r = read(fd, buffer, sizeof(buffer));
	} while (r == -1 && errno == EINTR);
	if (r < 1) return 0;
	bufferx = r;
	return 1;
}
static void bufferout(int fd)
{
	int r, x;

	if (fd == -1) return;
	for (x = 0; x < bufferx;) {
		do {
			r = write(fd, buffer+x, bufferx-x);
		} while (r == -1 && errno == EINTR);
		if (r < 1) exit(127); /* blech */
		x += r;
	}
}



static int selfpipe[2];
static void selfpipe_write(int signo)
{
	signal(SIGCHLD, selfpipe_write);
	(void) write(selfpipe[1], "\x01", 1);
}

struct child {
	int *a, *m, *i;
	pid_t p;
};

extern int fdmap(int m, fd_set *rset, fd_set *wset);
extern void fdset_copy(int m, fd_set *in, fd_set *out);

void make_child(struct child *c, const char *binshc, int need, int max,
		fd_set *r, fd_set *w, int *nmax, fd_set *out, int *tmp)
{
	int i, j, sv[2];

	c->a = (int *)malloc(sizeof(int) * need);
	c->i = (int *)malloc(sizeof(int) * need);
	c->m = (int *)malloc(sizeof(int) * max);
	if (!c->a || !c->m) exit(111);

	for (i = j = 0; i < max+1; i++) {
		if (FD_ISSET(i, r) || FD_ISSET(i, w)) {
			if (pair_duplex(sv) == -1) exit(112);
			c->m[i] = j;
			c->a[j] = sv[0];
			c->i[j] = i;
			FD_SET(sv[0], out);
			tmp[j] = sv[1];
			if (*nmax < sv[0]) *nmax = sv[0];
			j++;
		} else {
			c->m[i] = -1;
		}
	}

	c->p = fork();
	if (c->p == -1) exit(113);

	if (c->p == 0) {
		/* child */
		for (i = 0; i < max+1; i++) {
			if (!FD_ISSET(i, r) && !FD_ISSET(i, w)) continue;

			(void)close(i);
			j = tmp[ c->m[i] ];
			if (dup2(j, i) == -1) exit(127);
			if (fcntl(i, F_SETFD, 0) == -1) exit(127);
			(void) close(j);
		}
		/* okay, ready for child */
		execlp("/bin/sh", "sh", "-c", binshc, 0);
		exit(127); /* exec failed */
	}
}

int main(int argc, char *argv[])
{
	struct child *c;
	int x, i, j, m, p, nm, *tmp;
	char *q;
	fd_set r, w, rr, use;
	int children;

	m = -1;
	q = (char *)getenv("MAXFD");
	if (q) m = atoi(q);
	if (m < 0) m = 32;

	m = fdmap(m+1, &r, &w);
	for (i = p = 0; i <= m; i++) {
		if (FD_ISSET(i, &r) || FD_ISSET(i, &w)) p++;
	}

	if (pipe(selfpipe) == -1) exit(111);
	for (i = 0; i < 2; i++) {
		(void) fcntl(selfpipe[i], F_SETFL,
			     fcntl(selfpipe[i], F_GETFL, 0) | O_NONBLOCK);
	}
		     	
	signal(SIGCHLD, selfpipe_write);

	c = (struct child *)malloc(sizeof(struct child) * argc);
	tmp = (int *)malloc(sizeof(int) * p);
	if (!c || !tmp) exit(111);

	fdset_copy(m+1, &r, &rr);
	nm = m;
	for (i = 1, children = 0; i < argc; i++) {
		make_child(&c[i-1], argv[i], p, m+1, &r, &w, &nm, &rr, tmp);
		children++;
	}

	/* don't need this anymore */
	(void) free(tmp);

	if (nm < selfpipe[0]) nm = selfpipe[0];
	FD_SET(selfpipe[0], &rr);
	while (children > 0) {
		fdset_copy(nm+1, &rr, &use);
		do {
			x = select(nm+1, &use, 0, 0, 0);
		} while (x == -1 && errno == EINTR);
		for (i = 0; i < m+1; i++) {
			if (!FD_ISSET(i, &r) || !FD_ISSET(i, &use)) continue;
			if (bufferin(i)) {
				/* copy to outside */
				for (x = 1; x < argc; x++) {
					j = c[x-1].m[i];
					bufferout(c[x-1].a[j]);
				}
				continue;
			}
			/* okay, closed this; play close to children */
			for (x = 1; x < argc; x++) {
				j = c[x-1].m[i];
				if (c[x-1].a[j] > -1) {
					FD_CLR(c[x-1].a[j], &rr);
					shutdown(c[x-1].a[j], SHUT_RDWR);
					c[x-1].a[j] = -1;
				}
				if (c[x-1].p == -1) {
					c[x-1].p = 0;
					children--;
				}
			}
			FD_CLR(i, &rr);
			(void)close(i);
		}
		for (x = 1; x < argc; x++) {
			for (j = 0; j < p; j++) {
				if (c[x-1].a[j] == -1) continue;
				if (!FD_ISSET(c[x-1].a[j], &use)) continue;
				if (bufferin(c[x-1].a[j])) {
					bufferout(c[x-1].i[j]);
					continue;
				}

				if (c[x-1].a[j] > -1) {
					FD_CLR(c[x-1].a[j], &rr);
					shutdown(c[x-1].a[j], SHUT_RDWR);
					c[x-1].a[j] = -1;
				}
				if (c[x-1].p == -1) {
					c[x-1].p = 0;
					children--;
				}
			}
		}
		while ((x = waitpid(-1, &j, WNOHANG)) > 0) {
			if (x < 1) break;
			if (!WIFEXITED(j)) exit(127);
			if (WEXITSTATUS(j)) exit(WEXITSTATUS(j));
			for (j = 1; j < argc; j++) {
				if (c[j-1].p != x) continue;
				c[j-1].p = -1;
			}
		}
		if (FD_ISSET(selfpipe[0], &use)) {
			(void)bufferin(selfpipe[0]);
		}
	}
	exit(0); /* tada! */
}
