#ifndef PATH_H
#define PATH_H

#define PATH_SERVER_NAME "/sys/pathserver"

#define PATH_CMD_MKFILE 1
#define PATH_CMD_OPEN 2
#define PATH_CMD_REGISTER_PATH 3
#define PATH_CMD_REGISTER_FS 4
#define PATH_CMD_MOUNT 5

#define path_write_data(dst, src, len, pos) \
{ \
	memcpy(dst + pos, src, len); \
	pos += len; \
}

void pathserver();
int path_register(const char *pathname);
int path_register_fs(const char *type);
int mount(const char *src, const char *dst, const char *type, int flags);

#endif
