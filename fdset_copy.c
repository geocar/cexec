#include <sys/types.h>
#include <unistd.h>

void fdset_copy(int m, fd_set *in, fd_set *out)
{
	register int i;
	FD_ZERO(out);
	for (i = 0; i < m; i++) if (FD_ISSET(i,in)) FD_SET(i,out);
}
