// 3DS file systems have no extended attributes.
#pragma once
#include <errno.h>
#include <sys/types.h>
static inline ssize_t getxattr(const char *p, const char *n, void *v, size_t s) { (void)p; (void)n; (void)v; (void)s; errno = ENOTSUP; return -1; }
static inline int setxattr(const char *p, const char *n, const void *v, size_t s, int f) { (void)p; (void)n; (void)v; (void)s; (void)f; errno = ENOTSUP; return -1; }
static inline int removexattr(const char *p, const char *n) { (void)p; (void)n; errno = ENOTSUP; return -1; }
static inline ssize_t listxattr(const char *p, char *l, size_t s) { (void)p; (void)l; (void)s; errno = ENOTSUP; return -1; }
