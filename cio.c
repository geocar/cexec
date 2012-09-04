#include <errno.h>
#include <fcntl.h>

extern int sockop_tmp_block(int fd);
extern void sockop_nonblock(int fd, int n);

int cwrite(int fd, unsigned char *e, unsigned int len)
{
	int r, m;
	while (len > 0) {
		do {
			m = sockop_tmp_block(fd);
			do {
				r = write(fd, e, len);
			} while (r == -1 && errno == EINTR);
			sockop_nonblock(fd,m);
		} while (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
		if (r < 1) return 0;
		e += r; len -= r;
	}
	return 1;
}
int cread(int fd, unsigned char *e, unsigned int len)
{
	int r, m;
	while (len > 0) {
		do {
			m = sockop_tmp_block(fd);
			do {
				r = read(fd, e, len);
			} while (r == -1 && errno == EINTR);
			sockop_nonblock(fd,m);
		} while (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
		if (r < 1) return 0;
		e += r; len -= r;
	}
	return 1;
}

