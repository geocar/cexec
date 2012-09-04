/*
 * Multiple-precision ("very long") integer arithmetic
 *
 * References:
 *
 * 1.	Knuth, D. E.: "The Art of Computer Programming",
 *		2nd ed. (1981), vol. II (Seminumerical Algorithms), p. 257-258.
 *		Addison Wesley Publishing Company.
 *
 * 2.	Hansen, P. B.: "Multiple-length Division Revisited: a Tour of the Minefield".
 *		Software - Practice and Experience 24:6 (1994), 579-601.
 *
 * 3.	Menezes, A. J., van Oorschot, P. C., Vanstone, S. A.:
 *		"Handbook of Applied Cryptography", CRC Press (1997), section 14.2.5.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ec_param.h"
#include "ec_vlong.h"

int vl_equal (const vl_point p, const vl_point q)
{
	assert (p != NULL);
	assert (q != NULL);
	return memcmp (p, q, (p[0] + 1) * sizeof (word16)) == 0 ? 1 : 0;
} /* vl_equal */


void vl_clear (vl_point p)
{
	assert (p != NULL);
	memset (p, 0, sizeof (vl_point));
} /* vl_clear */


void vl_copy (vl_point p, const vl_point q)
	/* sets p := q */
{
	assert (p != NULL);
	assert (q != NULL);
	memcpy (p, q, (q[0] + 1) * sizeof (word16));
} /* vl_copy */


void vl_shortset (vl_point p, word16 u)
	/* sets p := u */
{
	assert (p != NULL);
	p[0] = 1; p[1] = u;
} /* vl_shortset */


int vl_numbits (const vl_point k)
	/* evaluates to the number of bits of k (index of most significant bit, plus one) */
{
	int i;
	word16 m, w;

	assert (k != NULL);
	if (k[0] == 0) {
		return 0;
	}
	w = k[k[0]]; /* last unit of k */
	for (i = (int)(k[0] << 4), m = 0x8000U; m; i--, m >>= 1) {
		if (w & m) {
			return i;
		}
	}
	return 0;
} /* vl_numbits */


int vl_takebit (const vl_point k, word16 i)
	/* evaluates to the i-th bit of k */
{
	assert (k != NULL);
	if (i >= (k[0] << 4)) {
		return 0;
	}
	return (int)((k[(i >> 4) + 1] >> (i & 15)) & 1);
} /* vl_takebit */


void vl_add (vl_point u, const vl_point v)
{
	word16 i;
	word32 t;

	assert (u != NULL);
	assert (v != NULL);
	/* clear high words of u if necessary: */
	for (i = u[0] + 1; i <= v[0]; i++) {
		u[i] = 0;
	}
    if (u[0] < v[0])
      u[0] = v[0];
	t = 0L;
	for (i = 1; i <= v[0]; i++) {
		t = t + (word32)u[i] + (word32)v[i];
		u[i] = (word16) (t & 0xFFFFUL);
		t >>= 16;
	}
    i = v[0]+1;
	while (t) {
        if ( i > u[0] )
        {
          u[i] = 0;
          u[0] += 1;
        }
        t = (word32)u[i] + 1;
		u[i] = (word16) (t & 0xFFFFUL);
        t >>= 16;
        i += 1;
	}
} /* vl_add */


void vl_subtract (vl_point u, const vl_point v)
{
	/* Assume u >= v */
	word32 carry = 0, tmp;
	int i;

	assert (u != NULL);
	assert (v != NULL);
	for (i = 1; i <= v[0]; i++) {
		tmp = 0x10000UL + (word32)u[i] - (word32)v[i] - carry;
		carry = 1;
		if (tmp >= 0x10000UL) {
			tmp -= 0x10000UL;
			carry = 0;
		}
		u[i] = (word16) tmp;
	}
	if (carry) {
		while (u[i] == 0) {
			i++;
		}
		u[i]--;
	}
	while (u[u[0]] == 0 && u[0]) {
		u[0]--;
	}
} /* vl_subtract */


void vl_shortlshift (vl_point p, int n)
{
	word16 i, T=0;

	assert (p != NULL);
	if (p[0] == 0) {
		return;
	}
	/* this will only work if 0 <= n <= 16 */
	if (p[p[0]] >> (16 - n)) {
		/* check if there is enough space for an extra unit: */
		if (p[0] <= VL_UNITS + 1) {
			++p[0];
			p[p[0]] = 0; /* just make room for one more unit */
		}
	}
	for (i = p[0]; i > 1; i--) {
		p[i] = (p[i] << n) | (p[i - 1] >> (16 - n));
	}
	p[1] <<= n;
} /* vl_shortlshift */


void vl_shortrshift (vl_point p, int n)
{
	word16 i;

	assert (p != NULL);
	if (p[0] == 0) {
		return;
	}
	/* this will only work if 0 <= n <= 16 */
	for (i = 1; i < p[0]; i++) {
		p[i] = (p[i + 1] << (16 - n)) | (p[i] >> n);
	}
	p[p[0]] >>= n;
	if (p[p[0]] == 0) {
		--p[0];
	}
} /* vl_shortrshift */


int vl_shortmultiply (vl_point p, const vl_point q, word16 d)
	/* sets p = q * d, where d is a single digit */
{
	int i;
	word32 t;

	assert (p != NULL);
	assert (q != NULL);
	if (q[0] > VL_UNITS) {
		puts ("ERROR: not enough room for multiplication\n");
		return -1;
	}
	if (d > 1) {
		t = 0L;
		for (i = 1; i <= q[0]; i++) {
			t += (word32)q[i] * (word32)d;
			p[i] = (word16) (t & 0xFFFFUL);
			t >>= 16;
		}
		if (t) {
			p[0] = q[0] + 1;
			p[p[0]] = (word16) (t & 0xFFFFUL);
		} else {
			p[0] = q[0];
		}
	} else if (d) { /* d == 1 */
		vl_copy (p, q);
	} else { /* d == 0 */
		p[0] = 0;
	}
	return 0;
} /* vl_shortmultiply */


int vl_greater (const vl_point p, const vl_point q)
{
	int i;

	assert (p != NULL);
	assert (q != NULL);
	if (p[0] > q[0]) return 1;
	if (p[0] < q[0]) return 0;
	for (i = p[0]; i > 0; i--) {
		if (p[i] > q[i]) return 1;
		if (p[i] < q[i]) return 0;
	}
	return 0;
} /* vl_greater */


void vl_remainder (vl_point u, const vl_point v)
{
	vl_point t;
	int shift = 0;

	assert (u != NULL);
	assert (v != NULL);
	assert (v[0] != 0);
	vl_copy( t, v );
	while ( vl_greater( u, t ) )
	{
		vl_shortlshift( t, 1 );
		shift += 1;
	}
	while ( 1 )
	{
		if ( vl_greater( t, u ) )
		{
			if (shift)
			{
				vl_shortrshift( t, 1 );
				shift -= 1;
			}
			else
				break;
		}
		else
			vl_subtract( u, t );
	}
} /* vl_remainder */

void vl_mulmod (vl_point u, const vl_point v, const vl_point w, const vl_point m)
{
	vl_point t;
	int i,j;
	
	assert (u != NULL);
	assert (v != NULL);
	assert (w != NULL);
	assert (m != NULL);
	assert (m[0] != 0);
	vl_clear( u );
	vl_copy( t, w );
	for (i=1;i<=v[0];i+=1)
	{
		for (j=0;j<16;j+=1)
		{
			if ( v[i] & (1u<<j) )
			{
				vl_add( u, t );
				vl_remainder( u, m );
			}
			vl_shortlshift( t, 1 );
			vl_remainder( t, m );
		}
	}
} /* vl_mulmod */
