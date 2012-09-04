#ifndef __bio_h
#define __bio_h

struct bio {
	unsigned int rp, wp;

	unsigned char *buffer;
	unsigned int bufsize;
};

int bio_setup_malloc(struct bio *b, unsigned int siz);
int bio_setup_buffer(struct bio *b, void *x, unsigned int xs);

#define bio_ready(b) ((b)->rp)
#define bio_skipout(b,x) ((b)->wp += x)
#define bio_skipin(b,x) ((b)->rp = x)

/* make sure input buffer has at least rs bytes filled */
int bio_need(int fd, struct bio *b, unsigned int rs);
/* fill as much as possible (in) resetting rp, wp */
int bio_fill(int fd, struct bio *b);
/* fill as much as possible (in) updating rp */
int bio_more(int fd, struct bio *b);

/* copy sz from in-buffer head, resetting wp and updating rp */
int bio_headin(struct bio *b, unsigned char *x, unsigned int sz);
/* copy sz into out-buffer head, updating wp */
int bio_headout(struct bio *b, unsigned char *x, unsigned int sz);

/* skip pos bytes, blasting out amu bytes from buffer */
int bio_blast(int fd, struct bio *b, unsigned int pos, unsigned int *amu);

/* like bio_blast but to multiple outputs */
int bio_spray(int *fd, int fdn, struct bio *b, unsigned int pos, unsigned int *amu);

/* copy until we have tot bytes written to out first from buffer, skipping fs
 * bytes...
 */
int bio_copy(int in, int out, struct bio *b, unsigned int fs, unsigned int tot);

/* like bio_copy but does it to multiple outs */
int bio_split(int in, int *outf, int outs, struct bio *b, unsigned int fs, unsigned int tot);

/* set base pointer so need starts here */
int bio_base(struct bio *b);

#endif
