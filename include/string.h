#ifndef STRING_H
#define STRING_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
int strcmp(const char *a, const char *b) __attribute__ ((naked));
size_t strlen(const char *s) __attribute__ ((naked));
char *strcpy(char *dest, const char *src);
int strncmp(const char *s1, const char *s2, size_t n);

#endif
