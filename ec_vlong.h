#ifndef __EC_VLONG_H
#define __EC_VLONG_H

#include <stdio.h>

#include "ec_param.h"

#include "types.h"

#define VL_UNITS ((GF_K*GF_L + 15)/16 + 1) /* must be large enough to hold a (packed) curve point (plus one element: the length) */

typedef word16 vl_point [VL_UNITS + 2];


void vl_clear (vl_point p);

void vl_shortset (vl_point p, word16 u);
	/* sets p := u */

int  vl_equal (const vl_point p, const vl_point q);

int  vl_greater (const vl_point p, const vl_point q);

int  vl_numbits (const vl_point k);
	/* evaluates to the number of bits of k (index of most significant bit, plus one) */

int  vl_takebit (const vl_point k, word16 i);
	/* evaluates to the i-th bit of k */

void vl_random (vl_point k);
	/* sets k := <random very long integer value> */

void vl_copy (vl_point p, const vl_point q);
	/* sets p := q */

void vl_add (vl_point u, const vl_point v);

void vl_subtract (vl_point u, const vl_point v);

void vl_remainder (vl_point u, const vl_point v);

void vl_mulmod (vl_point u, const vl_point v, const vl_point w, const vl_point m);

void vl_shortlshift (vl_point u, int n);

void vl_shortrshift (vl_point u, int n);

int  vl_shortmultiply (vl_point p, const vl_point q, word16 d);
	/* sets p = q * d, where d is a single digit */

#endif /* __EC_VLONG_H */
