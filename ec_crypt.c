/*
 * Elliptic curve cryptographic primitives
 */

#include "ec_curve.h"
#include "ec_vlong.h"
#include "ec_crypt.h"

void cp_makepublickey (vl_point vl_publickey, const vl_point vl_privatekey)
{
	ec_point ec_publickey;

	ec_copy (&ec_publickey, &curve_point);
	ec_multiply (&ec_publickey, vl_privatekey);
	ec_pack (&ec_publickey, vl_publickey);
} /* cp_makepublickey */


void cp_encodesecret (const vl_point vl_publickey, vl_point vl_message, vl_point vl_secret)
{
	ec_point q;

	ec_copy (&q, &curve_point); ec_multiply (&q, vl_secret); ec_pack (&q, vl_message);
	ec_unpack (&q, vl_publickey); ec_multiply (&q, vl_secret);	gf_pack (q.x, vl_secret);
} /* cp_makesecret */


void cp_decodesecret (const vl_point vl_privatekey, const vl_point vl_message, vl_point d)
{
	ec_point q;

		ec_unpack (&q, vl_message);	
	ec_multiply (&q, vl_privatekey);
	gf_pack(q.x, d);
} /* ec_decodesecret */

void cp_sign(const vl_point vl_privatekey, const vl_point k, const vl_point vl_mac, cp_pair * sig)
{
	ec_point q;
	vl_point tmp;
				
	ec_copy( &q, &curve_point );
	ec_multiply( &q, k);
	gf_pack(q.x, sig->r);
	vl_add( sig->r, vl_mac );
	vl_remainder( sig->r, prime_order );
	if ( sig->r[0] == 0 ) return;
	vl_mulmod( tmp, vl_privatekey, sig->r, prime_order );
	vl_copy( sig->s, k );
	if ( vl_greater( tmp, sig->s ) )
		vl_add( sig->s, prime_order );
	vl_subtract( sig->s, tmp );
} /* cp_sign */

int cp_verify(const vl_point vl_publickey, const vl_point vl_mac, cp_pair * sig )
{
	ec_point t1,t2;
	vl_point t3,t4;
	
	ec_copy( &t1, &curve_point );
	ec_multiply( &t1, sig->s );
	ec_unpack( &t2, vl_publickey );
	ec_multiply( &t2, sig->r );
	ec_add( &t1, &t2 );
	gf_pack( t1.x, t4 );
	vl_remainder( t4, prime_order );
	vl_copy( t3, sig->r );
	if ( vl_greater( t4, t3 ) )
		vl_add( t3, prime_order );
	vl_subtract( t3, t4 );
	return vl_equal( t3, vl_mac );
} /* cp_verify */
