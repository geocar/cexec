#include "keyfile.h"
#include "netnum.h"
#include "cio.h"

#include <string.h>

int vlpoint_read(vl_point k, int fd)
{
	unsigned char buf[2];
	unsigned int i, need;
	vl_point t;

	if (!cread(fd,buf,2)) return 0;
	need = get_uint16(buf);
	if (!need || need > VL_UNITS+1) return 0;

	k[0] = need;
	if (!cread(fd, (void *)&t[1], need*2)) return 0;
	for (i = 0; i < need; i++) {
		k[i+1] = get_uint16((char*)&t[i+1]);
	}
	return 1;
}

int vlpoint_write(vl_point k, int fd)
{
	vl_point w;
	unsigned int i;

	for (i = 0; i < k[0]+1; i++) {
		w[i] = 0;
		put_uint16((char*)&w[i], k[i]);
	}
	return cwrite(fd, (void *)w, (k[0]+1)*2);
}

int vlpoint_size(vl_point k)
{
	return (k[0]*2)+2;
}

int vlpoint_pack(vl_point k, unsigned char *str, int len)
{
	vl_point w;
	unsigned int i;

	if (len < vlpoint_size(k)) return 0;
	for (i = 0; i < k[0]+1; i++) {
		w[i] = 0;
		put_uint16((char*)&w[i], k[i]);
	}
	memcpy(str, w, vlpoint_size(k));
	return 1;
}

int vlpoint_unpack(vl_point k, unsigned char *str, int len)
{
	vl_point w;
	unsigned int i, need;

	if (len >= VL_UNITS) return 0;
	need = get_uint16(str);
	if (need >= VL_UNITS-1) return 0;

	for (i = 0; i < need+1; i++) {
		k[i] = get_uint16(str+(i*2));
	}

	return 1;
}
