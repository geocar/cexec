#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>

void sockop_nonblock(int fd, int n)
{
	if (n) {
		(void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	} else {
		(void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & (~O_NONBLOCK));
	}
}
int sockop_tmp_block(int fd)
{
	int n;
	n = fcntl(fd, F_GETFL, 0);
	if (n & O_NONBLOCK) {
		sockop_nonblock(fd,0);
		return 1;
	}
	return 0;
}
int sockop_tmp_nonblock(int fd)
{
	int n;
	n = fcntl(fd, F_GETFL, 0);
	if (!(n & O_NONBLOCK)) {
		sockop_nonblock(fd,1);
		return 0;
	}
	return 1;
}

void sockop_keepalive(int fd, int n)
{
	(void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &n, sizeof(n));
}
void sockop_ndelay(int fd, int n)
{
#ifdef SOL_TCP
	(void)setsockopt(fd, SOL_TCP, TCP_NODELAY, &n, sizeof(n));
#endif
}
void sockop_linger(int fd, int n)
{
	struct linger lin;
	lin.l_onoff = n ? 1 : 0;
	lin.l_linger = (n == 1 ? 120 : n);
	(void)setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
}
void sockop_oobinline(int fd, int n)
{
	(void)setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &n, sizeof(n));
}
void sockop_broadcast(int fd, int n)
{
	(void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &n, sizeof(n));
}
void sockop_reuseaddr(int fd, int n)
{
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
}
