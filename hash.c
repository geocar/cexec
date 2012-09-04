#include "ec_vlong.h"

#include <string.h>

unsigned int hash(const void *buf, unsigned int len)
{
	register const unsigned char *p = (const unsigned char *)buf;
	register const unsigned char *end = p + len;
	register unsigned int h = 5381;
	while (p < end) h = (h + (h << 5)) ^ *p++;
	return h;
}

#include "sha1.h"

/* this uses a double-barreled sha1 */
void hash_point(vl_point s1, vl_point s2, const void *buf, unsigned int len)
{
	struct SHA1_CONTEXT c[2];
	unsigned char *p;
	SHA1_DIGEST d[2];
	int i, j;

	memset(s1, 0, sizeof(vl_point));
	memset(s2, 0, sizeof(vl_point));

	sha1_context_init(&c[0]);
	sha1_context_init(&c[1]);

	sha1_context_hashstream(&c[0], buf, len);
	sha1_context_hashstream(&c[1], buf, len);
	sha1_context_hashstream(&c[1], buf, len);
	sha1_context_endstream(&c[0], len);
	sha1_context_endstream(&c[1], len);

	sha1_context_digest(&c[0], d[0]);
	sha1_context_digest(&c[1], d[1]);

	for (i = j = 0; i < sizeof(SHA1_DIGEST); i += 2, j++) {
		p = (unsigned char *)d[0];
		s1[1+j] = get_uint16( &p[i]);
		p = (unsigned char *)d[1];
		s2[1+j] = get_uint16( &p[i]);
	}
	s1[0] = s2[0] = j;
}
