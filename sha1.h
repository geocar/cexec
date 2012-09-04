#ifndef __sha1_h
#define __sha1_h

#define	SHA1_DIGEST_SIZE	20
#define	SHA1_BLOCK_SIZE		64


typedef	unsigned char SHA1_DIGEST[20];
typedef unsigned int	SHA1_WORD;

struct SHA1_CONTEXT {
	SHA1_WORD	H[5];

	unsigned char blk[SHA1_BLOCK_SIZE];
	unsigned blk_ptr;
};

void sha1_context_init(struct SHA1_CONTEXT *);
void sha1_context_hash(struct SHA1_CONTEXT *,
		const unsigned char[SHA1_BLOCK_SIZE]);
void sha1_context_hashstream(struct SHA1_CONTEXT *, const void *, unsigned);
void sha1_context_endstream(struct SHA1_CONTEXT *, unsigned int);
void sha1_context_digest(struct SHA1_CONTEXT *, SHA1_DIGEST);
void sha1_context_restore(struct SHA1_CONTEXT *, const SHA1_DIGEST);

void sha1_digest(const void *, unsigned, SHA1_DIGEST);
const char *sha1_hash(const char *);

#endif
