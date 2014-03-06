#include "romfs.h"

#include <stdint.h>
#include "syscall.h"
#include "path.h"
#include "fs.h"
#include "file.h"
#include "string.h"

struct romfs_file {
    int fd;
    int device;
    int start;
    size_t len;
};

struct romfs_entry {
    uint32_t parent;
    uint32_t prev;
    uint32_t next;
    uint32_t isdir;
    uint32_t len;
    uint8_t name[PATH_MAX];
};

int romfs_open_recur(int device, char *path, int this, struct romfs_entry *entry)
{
    if (entry->isdir) {
        /* Iterate through children */
        int pos = this + sizeof(*entry);
        while (pos) {
            /* Get entry */
            lseek(device, pos, SEEK_SET);
            read(device, entry, sizeof(*entry));

            /* Compare path */
            int len = strlen((char *)entry->name);
            if (strncmp((char *)entry->name, path, len) == 0) {
                if (path[len] == '/') { /* Match directory */
                    return romfs_open_recur(device, path + len + 1, pos, entry);
                }
                else if (path[len] == 0) { /* Match file */
                    return pos;
                }
            }

            /* Next entry */
            pos = entry->next;
        }
    }

    return -1;
}

/*
 * return entry position
 */
int romfs_open(int device, char *path, struct romfs_entry *entry)
{
    /* Get root entry */
    lseek(device, 0, SEEK_SET);
    read(device, entry, sizeof(*entry));

    return romfs_open_recur(device, path, 0, entry);
}

void romfs_server()
{
    struct romfs_file files[ROMFS_FILE_LIMIT];
    int nfiles = 0;
    int self = getpid() + 3;
    struct romfs_entry entry;
    struct fs_request request;
    int cmd;
    int from;
    int device;
    int target;
    int size;
    int pos;
    int status;
    int i;
    int data_start;
    int data_end;
    char data[REGFILE_BUF];

    path_register_fs(ROMFS_TYPE);

    while (1) {
        if (read(self, &request, sizeof(request)) == sizeof(request)) {
            cmd = request.cmd;
	        switch (cmd) {
	            case FS_CMD_OPEN:
	                device = request.device;
	                from = request.from;
	                pos = request.pos; /* searching starting position */
	                pos = romfs_open(request.device, request.path + pos, &entry);

	                if (pos >= 0) { /* Found */
	                    /* Register */
	                    status = path_register(request.path);

                        if (status != -1) {
                            mknod(status, 0, S_IFREG);
	                        files[nfiles].fd = status;
	                        files[nfiles].device = request.device;
	                        files[nfiles].start = pos + sizeof(entry);
	                        files[nfiles].len = entry.len;
	                        nfiles++;
	                    }
	                }
	                else {
	                    status = -1;
	                }

                    /* Response */
	                write(from, &status, sizeof(status));
	                break;
	            case FS_CMD_READ:
	                from = request.from;
	                target = request.target;
	                size = request.size;
	                pos = request.pos;

                    /* Find fd */
                    for (i = 0; i < nfiles; i++) {
                        if (files[i].fd == target) {
                            device = files[i].device;

                            /* Check boundary */
                            data_start = files[i].start + pos;
                            if (data_start < files[i].start) {
                                i = nfiles;
	                            break;
	                        }
                            if (data_start > files[i].start + files[i].len)
                                data_start = files[i].start + files[i].len;

                            data_end = data_start + size;
                            if (data_end > files[i].start + files[i].len)
                                data_end = files[i].start + files[i].len;
                            break;
                        }
                    }
                    if (i >= nfiles) {
                        write(target, NULL, -1);
                        break;
                    }

                    /* Get data from device */
                    lseek(device, data_start, SEEK_SET);
                    size = data_end - data_start;
                    size = read(device, data, size);

                    /* Response */
	                write(target, data, size);
	                break;

	            case FS_CMD_SEEK:
	                target = request.target;
	                size = request.size;
	                pos = request.pos;

                    /* Find fd */
                    for (i = 0; i < nfiles; i++) {
                        if (files[i].fd == target) {
                            break;
                        }
                    }
                    if (i >= nfiles) {
                        lseek(target, -1, SEEK_SET);
                        break;
                    }

	                if (pos == 0) { /* SEEK_SET */
	                }
	                else if (pos < 0) { /* SEEK_END */
	                    size = (files[i].len) + size;
	                }
	                else { /* SEEK_CUR */
	                    size = pos + size;
	                }
	                lseek(target, size, SEEK_SET);
	                break;

	            case FS_CMD_WRITE: /* readonly */
	            default:
	                write(target, NULL, -1);
	        }
        }
    }
}
