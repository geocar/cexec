
/* gathered from cpp -dM */
#if !defined(LINUX) && !defined(SOLARIS)
#if defined(__sun) || defined(__sun__) || defined(sun)
#define SOLARIS
#elif defined(__linux__) || defined(__linux) || defined(linux)
#define LINUX
#endif
#endif

#ifdef LINUX
#define _GNU_SOURCE
#include <stdlib.h>
#else

#ifdef FAKE_SOLARIS
#define getloadavg(x,y) (*(x)) = FAKE_SOLARIS
#else
#include <sys/loadavg.h>
#endif

#endif

#include <stdio.h>
#include <string.h>

int system_load(struct timeval *tv)
{
	double a;
	char buf[64], *q;

	getloadavg(&a,1);

	sprintf(buf,"%20.2lf", a);
	q = (char *)strchr(buf,'.');
	if (q) {
		*q = 0;
		tv->tv_usec = atoi(q+1);
		tv->tv_usec += atoi(q-1) * 100;
		q[-1] = 0;
		tv->tv_sec = atoi(buf);
	} else {
		tv->tv_sec = atoi(buf);
		tv->tv_usec = (tv->tv_sec % 100) * 100;
		tv->tv_sec /= 100;
	}
}

#ifdef TEST_HARNESS
main() {
	struct timeval tv;
	system_load(&tv);
	printf("%u.%u\n", tv.tv_sec, tv.tv_usec);
	exit(0);
}
#endif
