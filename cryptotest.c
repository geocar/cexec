#include "ec_crypt.h"
#include "ec_vlong.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	vl_point pub, sec, ix, s1, s2;
	cp_pair sig;
	unsigned char ck[32];
	int fd, i;

	gf_init();
	fd = open("test.key",O_RDONLY);
	if (fd==-1) exit(1);
	if (!vlpoint_read(sec, fd)) exit(1);
	(void)close(fd);
	fd = open("test.key.pub",O_RDONLY);
	if (fd==-1) exit(1);
	if (!vlpoint_read(pub, fd)) exit(1);
	(void)close(fd);

	/* blah */
	srand(time(0));
	for (i=0;i<32;i++) {
		ck[i] = rand() % 255;
	}
	hash_point(s1, s2, ck, 32);

	vl_clear(ix);
	vl_shortset(ix, 1);
	do {
		cp_sign(sec, s1, s2, &sig);
		vl_add(s1, ix);
	} while (sig.r[0] == 0);

	printf("s=");
	for (i = 0; i < sig.s[0]; i++) {
		printf("%04x", sig.s[i+1]);
	}
	printf("\nr=");
	for (i = 0; i < sig.r[0]; i++) {
		printf("%04x", sig.r[i+1]);
	}
	printf("\n");

	hash_point(s1, s2, ck, 32);

	if (!cp_verify(pub, s2, &sig)) {
		printf("does not match\n");
		exit(1);
	} else {
		printf("matches ok\n");
		exit(0);
	}
}
