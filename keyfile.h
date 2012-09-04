#ifndef __keyfile_h
#define __keyfile_h

#include "ec_vlong.h"

int vlpoint_read(vl_point k, int fd);
int vlpoint_write(vl_point k, int fd);

int vlpoint_size(vl_point k);

int vlpoint_pack(vl_point k, unsigned char *str, int len);
int vlpoint_unpack(vl_point k, unsigned char *str, int len);

#endif
