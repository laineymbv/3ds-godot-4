// 3DS has no dynamic loader; GDExtension and dlopen() are unavailable.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 4
#define RTLD_LOCAL 0
#define RTLD_NEXT ((void *)0)
static inline void *dlopen(const char *f, int m) { (void)f; (void)m; return 0; }
static inline int dlclose(void *h) { (void)h; return -1; }
static inline void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
static inline char *dlerror(void) { return (char *)"dynamic libraries are not supported on 3DS"; }
#ifdef __cplusplus
}
#endif
