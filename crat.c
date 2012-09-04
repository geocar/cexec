#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include "cluster.h"
#include "netnum.h"

#include "ec_crypt.h"
#include "ec_vlong.h"

#include "strline.h"

void usage(void)
{
	int i,r;
#define M "Usage: crat pubkey... > log\n"
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
int main(int argc, char *argv[])
{
	struct ip_mreq gamr;
	unsigned char buf[512];
	unsigned char *outline;
	unsigned char rattest[9];
	unsigned int hashbuf_size;
	unsigned char *hashbuf;
	struct sockaddr_in sin;
	unsigned char *hashes;
	unsigned int hv;
	vl_point pub;
	unsigned int outline_len, linelen;
	int i, r, fd;
	unsigned char *z;
	in_addr_t ina;
	char *q;

	if (argc < 1) usage();
	hashes = (unsigned char *)malloc(8 * (argc-1));
	if (!hashes)exit(111);
	for (i = 1, outline_len = 0; i < argc; i++) {
		r = strlen(argv[i]);
		if (r > outline_len) outline_len = r;
		fd = open(argv[i], O_RDONLY);
		if (fd == -1) {
			q = (char*)malloc(strlen(argv[i])+5);
			if (!q) exit(111);
			strcpy(q, argv[i]);
			strcat(q, ".pub");
			fd = open(q, O_RDONLY);
			if (fd == -1) usage();
			(void)free(q);
		}
		if (!vlpoint_read(pub, fd)) exit(111);
		(void)close(fd);
		
		hashbuf_size = vlpoint_size(pub);
		hashbuf = (char *)malloc(hashbuf_size);
		if (!hashbuf) exit(111);
		vlpoint_pack(pub, hashbuf, hashbuf_size);
		hv = hash(hashbuf, hashbuf_size);
		(void)free(hashbuf);
		put_uint32(hashes+((i-1)*4), hv);
	}
	outline_len += 15; /* strings */
	outline_len += 32; /* two ip addresses */
	outline_len += 128;/* some buffer space in case some bonehead isn't
			      paying attention... :) */
	outline = (char *)malloc(outline_len);
	if (!outline) exit(111);

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) exit(111);

	memset(&sin, 0, sizeof(sin));
	q = (char *)getenv("GROUP");
	if (!q) q = "255.255.255.255";
	ina = inet_addr(q);
	memcpy(&sin.sin_addr, &ina, sizeof(in_addr_t));

	i = *((unsigned char *)&ina);
	if (i >= 224 && i < 255) {
		/* multicast */
		memset(&gamr, 0, sizeof(gamr));
		memcpy(&gamr.imr_multiaddr, &ina, sizeof(in_addr_t));
		i = 1; (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &i, sizeof(i));
		i = 31; (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i));
		/* we might already have joined... */
		i=1;
		(void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				 &gamr,sizeof(gamr));
	}
	sockop_broadcast(fd, 1);
	sockop_reuseaddr(fd, 1);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(CLUSTER_PORT);
	if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) exit(111);

	for (;;) {
		i = sizeof(struct sockaddr_in);
		r = recvfrom(fd, buf, sizeof(buf), 0,
				(struct sockaddr *)&sin, &i);
		if (r != 9 && r != 8) continue; /* ignore this message */
		memcpy(rattest, buf, 4);
		q = 0;
		for (i = 1; i < argc; i++) {
			memcpy(rattest+4, hashes+((i-1)*4), 4);
			put_uint32(rattest+4, hash(rattest, 8));
			if (memcmp(rattest, buf, 8) == 0) {
				q = argv[i];
				break;
			}
		}

		z = outline;
		z += ip_print(z, sin.sin_addr.s_addr);
		if (r == 8) {
			z += static_str(z, " rats out ");
		} else {
			z += static_str(z, " completed by ");
		}
		memcpy(&hv, rattest, 4);
		z += ip_print(z, hv);
		z += static_str(z, " for ");
		if (q) {
			z += static_str(z, q);
		} else {
			z += static_str(z, "unknown key (0x");
			memcpy(&hv, buf+4, 4);
			z += hexnumber_print(z, hv);
			z += static_str(z, ")");
		}
		if (r == 9) {
			z += static_str(z, " exit code ");
			z += number_print(z, buf[8]);
		}
		z += static_str(z, "\n");

		linelen = z - outline;
		for (i = 0; i < linelen;) {
			do {
				r = write(1, outline+i, linelen-i);
			} while (r == -1 && errno == EINTR);
			if (r < 1) exit(255);
			i += r;
		}
	}
}

