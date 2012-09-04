#ifndef __netnum_h
#define __netnum_h

void put_uint32(unsigned char *x, unsigned int u);
void put_uint16(unsigned char *x, unsigned short u);
unsigned int get_uint32(unsigned char *x);
unsigned short get_uint16(unsigned char *x);

#endif
