#include "bio.h"

#include <sys/time.h>
#include <sys/types.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

/*
 *
 * bio->rp is the "read pointer" it's the limit on valid data. anything
 * located in the bio->buffer after bio->rp is invalid/useless.
 *
 * bio->wp is the "write cursor" it's only used on buffer outputs.
 *
 */

extern void fdset_copy(int m, fd_set *in, fd_set *out);

/* setup/allocate bio structure's buffer-space. */
int bio_setup_malloc(struct bio *b, unsigned int siz)
{
	void *q;
	q = malloc(siz);
	if (!q) return 0;
	return bio_setup_buffer(b, q, siz);
}
int bio_setup_buffer(struct bio *b, void *x, unsigned int xs)
{
	b->rp = b->wp = 0;
	b->buffer = (unsigned char *)x;
	b->bufsize = xs;

	/* always succeeds */
	return 1;
}

/* require that rs bytes be available on input */
int bio_need(int fd, struct bio *b, unsigned int rs)
{
	if (b->bufsize < rs) rs = b->bufsize;
	while (b->rp < rs) if (!bio_more(fd, b)) return 0;
	return 1;
}

/* attempt to make the bio buffer as full with NEW DATA as possible
 * won't discard old data on accident
 */
int bio_fill(int fd, struct bio *b)
{
	b->rp = b->wp = 0;
	return bio_more(fd, b);
}

/* get as much data into the bio-buffer as possible */
int bio_more(int fd, struct bio *b)
{
	int r;
	do {
		r = read(fd, b->buffer + b->rp, b->bufsize - b->rp);
	} while (r == -1 && errno == EINTR);
	if (r == -1 && errno == EAGAIN) return 1;
	if (r < 1) return 0;
	b->rp += r;
	return 1;
}

/* copy some read data into an extra buffer and move the write cursor 
 * used for stripping headers out of an input stream
 */
int bio_headin(struct bio *b, unsigned char *x, unsigned int sz)
{
	if (sz > b->bufsize) return 0;
	b->wp = sz;
	memcpy(x, b->buffer, sz);
	return 1;
}

/* prepare some header/space for an output */
int bio_headout(struct bio *b, unsigned char *x, unsigned int sz)
{
	if (sz > b->bufsize) return 0;
	b->wp = 0;
	memcpy(b->buffer, x, sz);
	return 1;
}

/* copy the buffer to output (fd) skipping pos bytes in input buffer
 * amu is treated as the limit (if it's available) and is decremented
 * by the amount actually moved.
 */
int bio_blast(int fd, struct bio *b, unsigned int pos, unsigned int *amu)
{
	int r, m, mfd;
	unsigned left;

	m = fcntl(fd, F_GETFL, 0);
	if (m & O_NONBLOCK) {
		(void)fcntl(mfd = fd, F_SETFL, m & (~O_NONBLOCK));
	}

	b->wp = pos;
	while (b->wp < b->rp && (!amu || *amu > 0)) {
		left = b->rp - b->wp;
		if (amu && *amu < left) left = *amu;
		if (fd == -1) {
			/* sink */
			r = left;
		} else {
			do {
				r = write(fd, b->buffer + b->wp, left);
			} while (r == -1 && errno == EINTR);
			if (r < 1) {
				fd = -1;
				continue;
			}
		}
		b->wp += r;
		if (amu) (*amu) -= r;
	}
	if (m & O_NONBLOCK) {
		(void)fcntl(mfd, F_SETFL, m);
	}
	return (fd == -1) ? 0 : 1;
}

/* copy data from in -> out using a bio buffer in the process.
 * "fs" is the number of bytes to skip from the input buffer (usually header)
 * tot is the total number of bytes to transfer.
 */
int bio_copy(int in, int out, struct bio *b, unsigned int fs, unsigned int tot)
{
	if (!bio_need(in, b, fs+(tot?1:tot))) return 0;
	if (!bio_blast(out, b, fs, &tot)) out = -1;
	while (tot > 0) {
		if (!bio_fill(in, b)) return 0;
		if (!bio_blast(out, b, 0, &tot)) out = -1;
	}
	return (out == -1) ? 0 : 1;
}

/* assume end of stream; reposition all internals to get whatever's left
 * in the buffer back at the beginning as a "new header" is what follows
 */
int bio_base(struct bio *b)
{
	if (b->rp > b->wp) {
		memmove(b->buffer, b->buffer+b->wp, b->rp - b->wp);
		b->rp = b->rp - b->wp;
		b->wp = 0;
		return 1;
	} else {
		b->rp = b->wp = 0;
		return 0;
	}
}
