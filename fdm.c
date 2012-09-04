#include <sys/select.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

int fdmap(int m, fd_set *rset, fd_set *wset)
{
	struct stat sb;
	struct timeval tv;
	struct pollfd pfd;
	int i, r, n;

	FD_ZERO(rset);
	FD_ZERO(wset);
	for (i = n = 0; i < m; i++) {
		do {
			errno = 0;
			r = fstat(i, &sb);
		} while (r == -1 && errno == EINTR); 
		if (errno == EBADF) continue;
		if (errno != 0) return -1; /* mur? */

		do {
			errno = 0;
			if (S_ISDIR(sb.st_mode)) {
				r = -1;
			} else if (S_ISSOCK(sb.st_mode)) {
				/* poll() can detect closed on both sun
				 * and linux; select() doesn't appear to
				 * work on linux
				 */
				pfd.fd = i;
				pfd.events = POLLHUP|POLLNVAL;
				pfd.revents = 0;
				r = poll(&pfd, 1, 0);
				if (r > -1) {
					errno = 0;
					if (pfd.revents & POLLHUP) {
						r = -1;
					} else {
						r = O_RDWR;
					}
					
				}
			} else if (S_ISCHR(sb.st_mode) && isatty(i)) {
				/* err... ttys are funny beasts... */
				if (i == 0) r = O_RDONLY;
				else r = O_WRONLY;
			} else {
				/* fifos, regular files, etc, really work */
				r = fcntl(i, F_GETFL, 0);
			}
		} while (errno == EINTR);
		if (r == -1 || errno == EBADF) continue;
		if (errno != 0) return -1; /* mur? */

#define PF(x) ((r & (x)) == x)
		if (PF(O_RDWR)) {
			FD_SET(i, rset);
			FD_SET(i, wset);
		} else if (PF(O_WRONLY)) {
			FD_SET(i, wset);
		} else if (PF(O_APPEND)) {
			FD_SET(i, wset);
		} else if (PF(O_RDONLY)) {
			FD_SET(i, rset);
		} else {
			/* nothing? */
			continue;
		}
#undef PF
		n=i;
	}
	return n;
}

#ifdef TEST_HARNESS
int main(int argc, char *argv[])
{
	int i, m;
	char *q;

	fd_set r,w;

	m = -1;
	q = (char *)getenv("MAXFD");
	if (q) m = atoi(q);
	if (m < 0) m = 32;

	m = fdmap(m+1, &r, &w);
	for (i = 0; i <= m; i++) {
		if (FD_ISSET(i, &r) && FD_ISSET(i, &w)) {
			printf("RW %d\n", i);
		} else if (FD_ISSET(i, &r)) {
			printf("R- %d\n", i);
		} else if (FD_ISSET(i, &w)) {
			printf("-W %d\n", i);
		} else {
			printf("-- %d\n", i);
		}
	}
	exit(0);
}
#endif
