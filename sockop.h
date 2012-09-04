#ifndef __sockop_h
#define __sockop_h
void sockop_broadcast(int fd, int n);
void sockop_reuseaddr(int fd, int n);
void sockop_nonblock(int fd, int n);
void sockop_keepalive(int fd, int n);
void sockop_linger(int fd, int n);
void sockop_oobinline(int fd, int n);
#endif
