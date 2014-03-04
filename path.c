#include "path.h"

#include "kconfig.h"
#include "string.h"
#include "syscall.h"

/* 
 * pathserver assumes that all files are FIFOs that were registered
 * with mkfifo.  It also assumes a global tables of FDs shared by all
 * processes.  It would have to get much smarter to be generally useful.
 *
 * The first TASK_LIMIT FDs are reserved for use by their respective tasks.
 * 0-2 are reserved FDs and are skipped.
 * The server registers itself at /sys/pathserver
 */
void pathserver()
{
	char paths[PIPE_LIMIT - TASK_LIMIT - 3][PATH_MAX];
	int npaths = 0;
	int i = 0;
	int cmd = 0;
	unsigned int plen = 0;
	unsigned int replyfd = 0;
	char path[PATH_MAX];
	int dev = 0;
	int newfd = 0;

	memcpy(paths[npaths++], PATH_SERVER_NAME, sizeof(PATH_SERVER_NAME));

	while (1) {
		read(PATHSERVER_FD, &cmd, 4);
		read(PATHSERVER_FD, &replyfd, 4);

		switch (cmd) {
		    case PATH_CMD_MKFILE:
		        read(PATHSERVER_FD, &plen, 4);
		        read(PATHSERVER_FD, path, plen);
			    read(PATHSERVER_FD, &dev, 4);
			    newfd = npaths + 3 + TASK_LIMIT;
			    if (mknod(newfd, 0, dev) == 0) {
			        memcpy(paths[npaths], path, plen);
			        npaths++;
			    }
			    else {
			        newfd = -1;
			    }
			    write(replyfd, &newfd, 4);
		        break;

		    case PATH_CMD_OPEN:
		        read(PATHSERVER_FD, &plen, 4);
		        read(PATHSERVER_FD, path, plen);
		        /* Search for path */
			    for (i = 0; i < npaths; i++) {
				    if (*paths[i] && strcmp(path, paths[i]) == 0) {
					    i += 3; /* 0-2 are reserved */
					    i += TASK_LIMIT; /* FDs reserved for tasks */
					    write(replyfd, &i, 4);
					    i = 0;
					    break;
				    }
			    }

			    if (i >= npaths) {
				    i = -1; /* Error: not found */
				    write(replyfd, &i, 4);
			    }
		        break;

		    case PATH_CMD_REGISTER_PATH:
		        read(PATHSERVER_FD, &plen, 4);
		        read(PATHSERVER_FD, path, plen);
			    newfd = npaths + 3 + TASK_LIMIT;
			    memcpy(paths[npaths], path, plen);
		        npaths++;
				write(replyfd, &newfd, 4);
		        break;

		    default:
		        ;
		}
	}
}

int path_register(const char *pathname)
{
	unsigned int reg_replyfd = getpid() + 3;
	size_t plen = strlen(pathname)+1;
	int fd = -1;
	char buf[4+4+PATH_MAX+4];

    /* Send replyfd = 0, plen, dev = 0, reg_replyfd */
	*((unsigned int *)buf) = 0;
	*((unsigned int *)(buf + 4)) = plen;
	memcpy(buf + 4 + 4, pathname, plen);
	*((int *)(buf + 4 + 4 + plen)) = 0;
	*((unsigned int *)(buf + 4 + 4 + plen + 4)) = reg_replyfd;
	write(PATHSERVER_FD, buf, 4 + 4 + plen + 4 + 4);
	read(reg_replyfd, &fd, 4);

	return fd;
}

