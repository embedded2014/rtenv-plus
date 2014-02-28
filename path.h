#ifndef PATH_H
#define PATH_H

#define PATH_SERVER_NAME "/sys/pathserver"

void pathserver();
int path_register(const char *pathname);

#endif
