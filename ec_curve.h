#ifndef __EC_CURVE_H
#define __EC_CURVE_H

#include <stddef.h>

#include "ec_field.h"
#include "ec_vlong.h"

#include "types.h"

typedef struct {
	gf_point x, y;
} ec_point;


extern const vl_point prime_order;
extern const ec_point curve_point;


int ec_check (const ec_point *p);
	/* confirm that y^2 + x*y = x^3 + EC_B for point p */

int ec_equal (const ec_point *p, const ec_point *q);
	/* evaluates to 1 if p == q, otherwise 0 (or an error code) */

void ec_copy (ec_point *p, const ec_point *q);
	/* sets p := q */

int ec_calcy (ec_point *p, int ybit);
	/* given the x coordinate of p, evaluate y such that y^2 + x*y = x^3 + EC_B */

void ec_random (ec_point *p);
	/* sets p to a random point of the elliptic curve defined by y^2 + x*y = x^3 + EC_B */

void ec_clear (ec_point *p);
	/* sets p to the point at infinity O, clearing entirely the content of p */

void ec_add (ec_point *p, const ec_point *r);
	/* sets p := p + r */

void ec_sub (ec_point *p, const ec_point *r);
	/* sets p := p - r */

void ec_negate (ec_point *p);
	/* sets p := -p */

void ec_double (ec_point *p);
	/* sets p := 2*p */

void ec_multiply (ec_point *p, const vl_point k);
	/* sets p := k*p */

int ec_ybit (const ec_point *p);
	/* evaluates to 0 if p->x == 0, otherwise to gf_ybit (p->y / p->x) */

void ec_pack (const ec_point *p, vl_point k);
	/* packs a curve point into a vl_point */

void ec_unpack (ec_point *p, const vl_point k);
	/* unpacks a vl_point into a curve point */

int ec_selftest (int test_count);
	/* perform test_count self tests */

#endif /* __EC_CURVE_H */
