#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

static int pfd = -1;
static unsigned char par;
static unsigned int pc;

void parity_init(void)
{
	pc = 0;
	pfd = open("/dev/urandom", O_RDONLY);
	if (pfd == -1) pfd = open("/dev/random", O_RDONLY);
	srand48(time(NULL) ^ getpid());
}
int parity(void)
{
	int p;
	int i;

	if (pfd > -1 && pc == 0) {
		if (read(pfd, &par, 1) != 1) {
			(void)close(pfd);
			pfd = -1;
		}
	}
	if (pfd > -1) {
		i = (par >> pc) & 1;
		pc++;
		if (pc == 8) pc = 0;
		return i;
	}

	p = lrand48();
	for (i = 0; i < 64; i++) p = (p >> 1) ^ p;
	return (int)(p & 1);
}
