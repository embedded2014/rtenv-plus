#include "file.h"

#include <stddef.h>
#include "kconfig.h"
#include "string.h"
#include "syscall.h"

int mkfile(const char *pathname, int mode, int dev)
{
	size_t plen = strlen(pathname)+1;
	char buf[4+4+PATH_MAX+4];
	(void) mode;

	*((unsigned int *)buf) = 0;
	*((unsigned int *)(buf + 4)) = plen;
	memcpy(buf + 4 + 4, pathname, plen);
	*((int *)(buf + 4 + 4 + plen)) = dev;
	write(PATHSERVER_FD, buf, 4 + 4 + plen + 4);

	return 0;
}

int open(const char *pathname, int flags)
{
	unsigned int replyfd = getpid() + 3;
	size_t plen = strlen(pathname) + 1;
	unsigned int fd = -1;
	char buf[4 + 4 + PATH_MAX];
	(void) flags;

	*((unsigned int *)buf) = replyfd;
	*((unsigned int *)(buf + 4)) = plen;
	memcpy(buf + 4 + 4, pathname, plen);
	write(PATHSERVER_FD, buf, 4 + 4 + plen);
	read(replyfd, &fd, 4);

	return fd;
}
