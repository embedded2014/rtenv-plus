#include "romdev.h"

#include "syscall.h"
#include "block.h"
#include "path.h"


void romdev_driver()
{
    extern const char _sromdev;
    extern const char _eromdev;
    int self;
	int fd;
    struct block_request request;
    int cmd;
    int task;
    size_t size;
    int pos;
    const char *request_start;
    const char *request_end;
    size_t request_len;

    /* Register path for device */
    self = getpid();
	fd = path_register(ROMDEV_PATH);
	mknod(fd, 0, S_IFBLK);

    /* Service routine */
	while (1) {
	    if (read(self, &request, sizeof(request)) == sizeof(request)) {
	        cmd = request.cmd;

	        if (cmd == BLOCK_CMD_READ) {
	            task = request.task;
	            fd = request.fd;
	            size = request.size;
	            pos = request.pos;

                /* Check boundary */
                request_start = &_sromdev + pos;
                if (request_start < &_sromdev)
	                block_response(fd, NULL, -1);
                if (request_start > &_eromdev)
                    request_start = &_eromdev;

                request_end = request_start + size;
                if (request_end > &_eromdev)
                    request_end = &_eromdev;

                /* Response */
                request_len = request_end - request_start;
	            block_response(fd, (char *)request_start, request_len);
	        }
	        else { /* readonly */
	            block_response(fd, NULL, -1);
	        }
	    }
	}
}
