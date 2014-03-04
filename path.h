#ifndef PATH_H
#define PATH_H

#define PATH_SERVER_NAME "/sys/pathserver"

#define PATH_CMD_MKFILE 1
#define PATH_CMD_OPEN 2
#define PATH_CMD_REGISTER_PATH 3

#define path_write_data(dst, src, len, pos) \
{ \
	memcpy(dst + pos, src, len); \
	pos += len; \
}

void pathserver();
int path_register(const char *pathname);

#endif
