#ifndef __EC_FIELD_H
#define __EC_FIELD_H

#include <stdio.h>

#include "ec_param.h"
#include "ec_vlong.h"

#include "types.h"

#define GF_POINT_UNITS	(2*(GF_K+1))

#if GF_L < 8 || GF_L > 16
	#error "this implementation assumes 8 <= GF_L <= 16"
#endif

#if GF_L ==  8
	#define BITS_PER_LUNIT 8
	typedef byte	lunit;
#else
	#define BITS_PER_LUNIT 16
	typedef word16	lunit;
#endif

#if GF_L == 16
	typedef word32	ltemp;
#else
	typedef word16	ltemp;
#endif

typedef lunit gf_point [GF_POINT_UNITS];

/* interface functions: */

int  gf_init (void);
	/* initialize the library ---> MUST be called before any other gf-function */

void gf_quit (void);
	/* perform housekeeping for library termination */

int  gf_equal (const gf_point p, const gf_point q);
	/* evaluates to 1 if p == q, otherwise 0 (or an error code) */

void gf_clear (gf_point p);
	/* sets p := 0, clearing entirely the content of p */

void gf_random (gf_point p);
	/* sets p := <random field element> */

void gf_copy (gf_point p, const gf_point q);
	/* sets p := q */

void gf_add (gf_point p, const gf_point q, const gf_point r);
	/* sets p := q + r */

void gf_multiply (gf_point r, const gf_point p, const gf_point q);
	/* sets r := p * q mod (x^GF_K + x^GF_T + 1) */

void gf_smalldiv (gf_point p, lunit b);
	/* sets p := (b^(-1))*p mod (x^GF_K + x^GF_T + 1) for b != 0 (of course...) */

void gf_square (gf_point p, const gf_point q);
	/* sets p := q^2 mod (x^GF_K + x^GF_T + 1) */

int  gf_invert (gf_point p, const gf_point q);
	/* sets p := q^(-1) mod (x^GF_K + x^GF_T + 1) */
	/* warning: p and q must not overlap! */

void gf_squareroot (gf_point p, lunit b);
	/* sets p := sqrt(b) = b^(2^(GF_M-1)) */

int  gf_trace (const gf_point p);
	/* quickly evaluates to the trace of p (or an error code) */

int  gf_quadsolve (gf_point p, const gf_point q);
	/* sets p to a solution of p^2 + p = q */

int  gf_ybit (const gf_point p);
	/* evaluates to the rightmost (least significant) bit of p (or an error code) */

void gf_pack (const gf_point p, vl_point k);
	/* packs a field point into a vl_point */

void gf_unpack (gf_point p, const vl_point k);
	/* unpacks a vl_point into a field point */

int  gf_selftest (int test_count);
	/* perform test_count self tests */

#endif /* __EC_FIELD_H */

