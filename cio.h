#ifndef __cio_h
#define __cio_h

int cwrite(int fd, unsigned char *e, unsigned int len);
int cread(int fd, unsigned char *e, unsigned int len);

#endif
