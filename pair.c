/*
 * note, at some point in the future, using TTYs instead of socketpairs
 * might be advisble.
 *
 * at present, the TTY code doesn't work. I haven't tracked it down, and
 * I don't presently need it so it's not a big deal.
 *
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#ifdef USE_OPENPTY
#include <pty.h>
#endif

#ifdef USE_STREAMS
#include <stropts.h>
#endif

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif

int pair_simplex(int sv[2])
{
	return pipe(sv);
}

int pair_duplex(int sv[2])
{
#if defined(USE_OPENPTY)
	return openpty(&sv[0], &sv[1], (char*)0,
			(struct termios *)0, (struct winsize *)0);

#elif defined(USE_STREAMS)

	int fd, ptm, tty;
	char *pts;

	ptm = open("/dev/ptmx", O_RDWR|O_NOCTTY);
	if (ptm < 0) return -1;
	if (grantpt(ptm) < 0 || unlockpt(ptm)) {
		(void)close(ptm);
		return -1;
	}
	pts = (char *)ptsname(ptm);
	if (!pts) {
		(void)close(ptm);
		return -1;
	}

	tty = open(pts, O_RDWR|O_NOCTTY);
	if (ioctl(tty, I_PUSH, "pterm") < 0
			|| ioctl(tty, I_PUSH, "ldterm") < 0) {
		(void)close(tty);
		(void)close(ptm);
		return -1;
	}
	(void)ioctl(tty, I_PUSH, "ttcompat");

	sv[0] = ptm;
	sv[1] = tty;
	return 0;
#else
	struct linger lin;
	int r, i;
	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, sv) == -1) return -1;
	for (i = 0; i < 2; i++) {
		sockop_keepalive(sv[i], 1);
		sockop_ndelay(sv[i], 1);
		sockop_linger(sv[i], 1);
		sockop_oobinline(sv[i], 1);
	}
	return 0;
#endif
}
