/*
 * Elliptic curves over GF(2^m)
 */

#include <stdlib.h>
#include <time.h>

#include "ec_curve.h"
#include "ec_field.h"
#include "ec_param.h"
#include "ec_vlong.h"

extern const vl_point prime_order;
extern const ec_point curve_point;


int ec_equal (const ec_point *p, const ec_point *q)
	/* evaluates to 1 if p == q, otherwise 0 (or an error code) */
{
	return gf_equal (p->x, q->x) && gf_equal (p->y, q->y);
} /* ec_equal */

void ec_clear (ec_point *p)
	/* sets p to the point at infinity O, clearing entirely the content of p */
{
	gf_clear (p->x);
	gf_clear (p->y);
} /* ec_clear */


void ec_copy (ec_point *p, const ec_point *q)
	/* sets p := q */
{
	gf_copy (p->x, q->x);
	gf_copy (p->y, q->y);
} /* ec_copy */


int ec_calcy (ec_point *p, int ybit)
	/* given the x coordinate of p, evaluate y such that y^2 + x*y = x^3 + EC_B */
{
	gf_point a, b, t;

	b[0] = 1; b[1] = EC_B;
	if (p->x[0] == 0) {
		/* elliptic equation reduces to y^2 = EC_B: */
		gf_squareroot (p->y, EC_B);
		return 1;
	}
	/* evaluate alpha = x^3 + b = (x^2)*x + EC_B: */
	gf_square (t, p->x); /* keep t = x^2 for beta evaluation */
	gf_multiply (a, t, p->x);
	gf_add (a, a, b); /* now a == alpha */
	if (a[0] == 0) {
		p->y[0] = 0;
		/* destroy potentially sensitive data: */
		gf_clear (a); gf_clear (t);
		return 1;
	}
	/* evaluate beta = alpha/x^2 = x + EC_B/x^2 */
	gf_smalldiv (t, EC_B);
	gf_invert (a, t);
	gf_add (a, p->x, a); /* now a == beta */
	/* check if a solution exists: */
	if (gf_trace (a) != 0) {
		/* destroy potentially sensitive data: */
		gf_clear (a); gf_clear (t);
		return 0; /* no solution */
	}
	/* solve equation t^2 + t + beta = 0 so that gf_ybit(t) == ybit: */
	gf_quadsolve (t, a);
	if (gf_ybit (t) != ybit) {
		t[1] ^= 1;
	}
	/* compute y = x*t: */
	gf_multiply (p->y, p->x, t);
	/* destroy potentially sensitive data: */
	gf_clear (a); gf_clear (t);
	return 1;
} /* ec_calcy */


void ec_add (ec_point *p, const ec_point *q)
	/* sets p := p + q */
{
	gf_point lambda, t, tx, ty, x3;

	/* first check if there is indeed work to do (q != 0): */
	if (q->x[0] != 0 || q->y[0] != 0) {
		if (p->x[0] != 0 || p->y[0] != 0) {
			/* p != 0 and q != 0 */
			if (gf_equal (p->x, q->x)) {
				/* either p == q or p == -q: */
				if (gf_equal (p->y, q->y)) {
					/* points are equal; double p: */
					ec_double (p);
				} else {
					/* must be inverse: result is zero */
					/* (should assert that q->y = p->x + p->y) */
					p->x[0] = p->y[0] = 0;
				}
			} else {
				/* p != 0, q != 0, p != q, p != -q */
				/* evaluate lambda = (y1 + y2)/(x1 + x2): */
				gf_add (ty, p->y, q->y);
				gf_add (tx, p->x, q->x);
				gf_invert (t, tx);
				gf_multiply (lambda, ty, t);
				/* evaluate x3 = lambda^2 + lambda + x1 + x2: */
				gf_square (x3, lambda);
				gf_add (x3, x3, lambda);
				gf_add (x3, x3, tx);
				/* evaluate y3 = lambda*(x1 + x3) + x3 + y1: */
				gf_add (tx, p->x, x3);
				gf_multiply (t, lambda, tx);
				gf_add (t, t, x3);
				gf_add (p->y, t, p->y);
				/* deposit the value of x3: */
				gf_copy (p->x, x3);
			}
		} else {
			/* just copy q into p: */
			gf_copy (p->x, q->x);
			gf_copy (p->y, q->y);
		}
	}
} /* ec_add */


void ec_sub (ec_point *p, const ec_point *r)
	/* sets p := p - r */
{
	ec_point t;

	gf_copy (t.x, r->x);
	gf_add  (t.y, r->x, r->y);
	ec_add (p, &t);
} /* ec_sub */

void ec_negate (ec_point *p)
	/* sets p := -p */
{
	gf_add (p->y, p->x, p->y);
} /* ec_negate */

void ec_double (ec_point *p)
	/* sets p := 2*p */
{
	gf_point lambda, t1, t2;

	/* evaluate lambda = x + y/x: */
	gf_invert (t1, p->x);
	gf_multiply (lambda, p->y, t1);
	gf_add (lambda, lambda, p->x);
	/* evaluate x3 = lambda^2 + lambda: */
	gf_square (t1, lambda);
	gf_add (t1, t1, lambda); /* now t1 = x3 */
	/* evaluate y3 = x^2 + lambda*x3 + x3: */
	gf_square (p->y, p->x);
	gf_multiply (t2, lambda, t1);
	gf_add (p->y, p->y, t2);
	gf_add (p->y, p->y, t1);
	/* deposit the value of x3: */
	gf_copy (p->x, t1);
} /* ec_double */


void ec_multiply (ec_point *p, const vl_point k)
	/* sets p := k*p */
{
	vl_point h;
	int z, hi, ki;
	word16 i;
	ec_point r;

	gf_copy (r.x, p->x); p->x[0] = 0;
	gf_copy (r.y, p->y); p->y[0] = 0;
	vl_shortmultiply (h, k, 3);
	z = vl_numbits (h) - 1; /* so vl_takebit (h, z) == 1 */
	i = 1;
	for (;;) {
		hi = vl_takebit (h, i);
		ki = vl_takebit (k, i);
		if (hi == 1 && ki == 0) {
			ec_add (p, &r);
		}
		if (hi == 0 && ki == 1) {
			ec_sub (p, &r);
		}
		if (i >= z) {
			break;
		}
		i++;
		ec_double (&r);
	}
} /* ec_multiply */


int ec_ybit (const ec_point *p)
	/* evaluates to 0 if p->x == 0, otherwise to gf_ybit (p->y / p->x) */
{
	gf_point t1, t2;

	if (p->x[0] == 0) {
		return 0;
	} else {
		gf_invert (t1, p->x);
		gf_multiply (t2, p->y, t1);
		return gf_ybit (t2);
	}
} /* ec_ybit */


void ec_pack (const ec_point *p, vl_point k)
	/* packs a curve point into a vl_point */
{
	vl_point a;

	if (p->x[0]) {
		gf_pack (p->x, k);
		vl_shortlshift (k, 1);
		vl_shortset (a, (word16) ec_ybit (p));
		vl_add (k, a);
	} else if (p->y[0]) {
		vl_shortset (k, 1);
	} else {
		k[0] = 0;
	}
} /* ec_pack */


void ec_unpack (ec_point *p, const vl_point k)
	/* unpacks a vl_point into a curve point */
{
	int yb;
	vl_point a;

	vl_copy (a, k);
	yb = a[0] ? a[1] & 1 : 0;
	vl_shortrshift (a, 1);
	gf_unpack (p->x, a);

	if (p->x[0] || yb) {
		ec_calcy (p, yb);
	} else {
		p->y[0] = 0;
	}
} /* ec_unpack */
