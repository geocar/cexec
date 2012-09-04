#include "netnum.h"

void put_uint32(unsigned char *x, unsigned int u)
{
	*x++ = (u&255); u >>= 8;
	*x++ = (u&255); u >>= 8;
	*x++ = (u&255); u >>= 8;
	*x++ = (u&255);
}
void put_uint16(unsigned char *x, unsigned short u)
{
	*x++ = (u&255); u >>= 8;
	*x++ = (u&255);
}
unsigned int get_uint32(unsigned char *x)
{
	return (x[0]&255)
		| ((x[1]&255) << 8)
		| ((x[2]&255) << 16)
		| ((x[3]&255) << 24);
}
unsigned short get_uint16(unsigned char *x)
{
	return (x[0]&255) | ((x[1]&255) << 8);
}
