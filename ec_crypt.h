#ifndef __EC_CRYPT_H
#define __EC_CRYPT_H

#include "ec_curve.h"
#include "ec_vlong.h"

typedef struct {
	vl_point r, s;
} cp_pair;


void cp_makepublickey (vl_point vl_publickey, const vl_point vl_privatekey);
void cp_encodesecret (const vl_point vl_publickey, vl_point vl_message, vl_point vl_secret);
void cp_decodesecret (const vl_point vl_privatekey, const vl_point vl_message, vl_point d);
void cp_sign(const vl_point vl_privatekey, const vl_point secret, const vl_point mac, cp_pair * cp_sig);
int  cp_verify(const vl_point vl_publickey, const vl_point vl_mac, cp_pair * cp_sig );

#endif /* __EC_CRYPT_H */
