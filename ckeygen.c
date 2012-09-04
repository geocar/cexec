#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "sha1.h"
#include "keyfile.h"

static void usage(int e)
{
	int i, r;
#define M "Usage: ckeygen [-stdin] app [app.pub]\n"
	for (i = 0; i < sizeof(M)-1;) {
		do {
			r = write(2, M+i,(sizeof(M)-1)-i);
		} while (r == -1 && errno == EINTR);
		if (r < 1) exit(255);
		i += r;
	}
	exit(e);
#undef M
}

int main(int argc, char *argv[])
{
	vl_point sec, pub, w;
	word16 kk;
	char *sf, *pf;
	int fd, i, j;
	int use_stdin = 0;

	parity_init();
	gf_init();

	if (argc > 1 && strcmp(argv[1], "-stdin") == 0) {
		use_stdin = 1;
		argv++;
		argc--;
	}


	if (argc == 1 || argc > 3) usage(0);

	if (use_stdin) {
		struct SHA1_CONTEXT c;
		SHA1_DIGEST dg;
		unsigned int total;
		char buffer[4096];

		vl_clear(pub);
		sha1_context_init(&c);
		for (total = 0;;) {
			do {
				i = read(0, buffer, sizeof(buffer));
			} while (i == -1 && errno == EINTR);
			if (i == 0) break;
			if (i < 1) exit(255);
			sha1_context_hashstream(&c, buffer, i);
			total += i;
		}
		sha1_context_endstream(&c, total);
		sha1_context_digest(&c, dg);
		w[0] = 1;
		for (i = 0; i < sizeof(SHA1_DIGEST); i++) {
			w[1] = dg[i];
			vl_shortlshift(pub, 8);
			vl_add(pub, w);
		}
		/* k... digest turned into key */
	}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		unsigned int bytes;

		vl_clear(sec);
		if (use_stdin) {
			/* copy */
			for (i = 0; i <= pub[0]; i++) {
				sec[i] = pub[i];
			}
		} else {
			sec[0] = VL_UNITS-1;

			fd = open("/dev/random", O_RDONLY);
			if (fd == -1) {
				w[0] = 1;
				for (i = 0; i < sec[0]; i++) {
					vl_shortlshift(sec, 16);
					kk = ((unsigned int)parity())
						| (((unsigned int)parity()) << 1)
						| (((unsigned int)parity()) << 2)
						| (((unsigned int)parity()) << 3)
						| (((unsigned int)parity()) << 4)
						| (((unsigned int)parity()) << 5)
						| (((unsigned int)parity()) << 6)
						| (((unsigned int)parity()) << 7)
						| (((unsigned int)parity()) << 8)
						| (((unsigned int)parity()) << 9)
						| (((unsigned int)parity()) << 10)
						| (((unsigned int)parity()) << 11)
						| (((unsigned int)parity()) << 12)
						| (((unsigned int)parity()) << 13)
						| (((unsigned int)parity()) << 14)
						| (((unsigned int)parity()) << 15);
					memcpy(&w[1], &kk, sizeof(word16));
					vl_add(sec, w);
				}
			} else {
				bytes = VL_UNITS * sizeof(word16);
				for (i = 0; i < bytes;) {
					j = read(fd, (&sec[1])+i, bytes - i);
					if (j > 0) i += j; 
				}
				(void)close(fd);
			}
		}
	} else if (!vlpoint_read(sec, fd)) {
		exit(101);
	} else {
		if (use_stdin) {
			if (pub[0] != sec[0]) exit(104);
			for (i = 1; i <= pub[0]; i++)
				if (pub[i] != sec[i]) exit(104);
			/* kay, they match */
		}
		(void)close(fd);
	}
	
	sf = argv[1];
	if (argc == 2) {
		pf = (char *)malloc(strlen(sf) + 4);
		if (!pf) exit(111);
		strcpy(pf, sf);
		strcat(pf, ".pub");
	} else {
		pf = argv[2];
	}

	vl_clear(pub);
	cp_makepublickey(pub, sec);

	fd = open(pf, O_WRONLY|O_CREAT, 0666);
	if (fd == -1) exit(102);
	if (!vlpoint_write(pub, fd)) exit(102);
	close(fd);

	fd = open(sf, O_WRONLY|O_CREAT, 0666);
	if (fd == -1) exit(103);
	if (!vlpoint_write(sec, fd)) exit(103);
	close(fd);

	exit(0);
}
