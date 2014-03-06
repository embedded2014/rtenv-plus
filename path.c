#include "path.h"

#include "kconfig.h"
#include "string.h"
#include "syscall.h"
#include "fs.h"

struct mount {
    int fs;
    int dev;
    char path[PATH_MAX];
};

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
	char paths[FILE_LIMIT - TASK_LIMIT - 3][PATH_MAX];
	int npaths = 0;
	int fs_fds[FS_LIMIT];
	char fs_types[FS_LIMIT][FS_TYPE_MAX];
	int nfs_types = 0;
	struct mount mounts[MOUNT_LIMIT];
	int nmounts = 0;
	int i = 0;
	int cmd = 0;
	unsigned int plen = 0;
	unsigned int replyfd = 0;
	char path[PATH_MAX];
	int dev = 0;
	int newfd = 0;
	char fs_type[FS_TYPE_MAX];
	int status;

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

			    if (i < npaths) {
				    break;
			    }

		        /* Search for mount point */
			    for (i = 0; i < nmounts; i++) {
				    if (*mounts[i].path
				            && strncmp(path, mounts[i].path,
				                       strlen(mounts[i].path)) == 0) {
				        int mlen = strlen(mounts[i].path);
					    struct fs_request request;
					    request.cmd = FS_CMD_OPEN;
					    request.from = replyfd;
					    request.device = mounts[i].dev;
					    request.pos = mlen; /* search starting position */
					    memcpy(request.path, &path, plen);
					    write(mounts[i].fs, &request, sizeof(request));
					    i = 0;
					    break;
				    }
			    }

			    if (i >= nmounts) {
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

		    case PATH_CMD_REGISTER_FS:
		        read(PATHSERVER_FD, &plen, 4);
		        read(PATHSERVER_FD, fs_type, plen);
		        fs_fds[nfs_types] = replyfd;
			    memcpy(fs_types[nfs_types], fs_type, plen);
		        nfs_types++;
		        i = 0;
				write(replyfd, &i, 4);
				break;

			case PATH_CMD_MOUNT: {
			    int slen;
			    int dlen;
			    int tlen;
			    char src[PATH_MAX];
			    char dst[PATH_MAX];
			    char type[FS_TYPE_MAX];
		        read(PATHSERVER_FD, &slen, 4);
		        read(PATHSERVER_FD, src, slen);
		        read(PATHSERVER_FD, &dlen, 4);
		        read(PATHSERVER_FD, dst, dlen);
		        read(PATHSERVER_FD, &tlen, 4);
		        read(PATHSERVER_FD, type, tlen);

		        /* Search for filesystem types */
			    for (i = 0; i < nfs_types; i++) {
				    if (*fs_types[i] && strcmp(type, fs_types[i]) == 0) {
					    break;
				    }
			    }

			    if (i >= nfs_types) {
				    status = -1; /* Error: not found */
				    write(replyfd, &status, 4);
				    break;
			    }

                mounts[nmounts].fs = fs_fds[i];

		        /* Search for device */
			    for (i = 0; i < npaths; i++) {
				    if (*paths[i] && strcmp(src, paths[i]) == 0) {
					    break;
				    }
			    }

			    if (i >= npaths) {
				    status = -1; /* Error: not found */
				    write(replyfd, &status, 4);
				    break;
			    }

                /* Store mount point */
                mounts[nmounts].dev = i + 3 + TASK_LIMIT;
			    memcpy(mounts[nmounts].path, dst, dlen);
			    nmounts++;

                status = 0;
                write(replyfd, &status, 4);
		    }   break;

		    default:
		        ;
		}
	}
}

int path_register(const char *pathname)
{
    int cmd = PATH_CMD_REGISTER_PATH;
	unsigned int replyfd = getpid() + 3;
	size_t plen = strlen(pathname)+1;
	int fd = -1;
	char buf[4+4+4+PATH_MAX];
	int pos = 0;

	path_write_data(buf, &cmd, 4, pos);
	path_write_data(buf, &replyfd, 4, pos);
	path_write_data(buf, &plen, 4, pos);
	path_write_data(buf, pathname, plen, pos);

	write(PATHSERVER_FD, buf, pos);
	read(replyfd, &fd, 4);

	return fd;
}

int path_register_fs(const char *type)
{
    int cmd = PATH_CMD_REGISTER_FS;
	unsigned int replyfd = getpid() + 3;
	size_t plen = strlen(type)+1;
	int fd = -1;
	char buf[4+4+4+PATH_MAX];
	int pos = 0;

	path_write_data(buf, &cmd, 4, pos);
	path_write_data(buf, &replyfd, 4, pos);
	path_write_data(buf, &plen, 4, pos);
	path_write_data(buf, type, plen, pos);

	write(PATHSERVER_FD, buf, pos);
	read(replyfd, &fd, 4);

	return fd;
}

int mount(const char *src, const char *dst, const char *type, int flags)
{
    int cmd = PATH_CMD_MOUNT;
	unsigned int replyfd = getpid() + 3;
	size_t slen = strlen(src)+1;
	size_t dlen = strlen(dst) + 1;
	size_t tlen = strlen(type) + 1;
	int status;
	char buf[4 + 4 + 4 + PATH_MAX + 4 + PATH_MAX + 4 + FS_TYPE_MAX];
	int pos = 0;

	path_write_data(buf, &cmd, 4, pos);
	path_write_data(buf, &replyfd, 4, pos);
	path_write_data(buf, &slen, 4, pos);
	path_write_data(buf, src, slen, pos);
	path_write_data(buf, &dlen, 4, pos);
	path_write_data(buf, dst, dlen, pos);
	path_write_data(buf, &tlen, 4, pos);
	path_write_data(buf, type, tlen, pos);

	write(PATHSERVER_FD, buf, pos);
	read(replyfd, &status, 4);

	return status;
}

