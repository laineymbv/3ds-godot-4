// newlib's <sys/resource.h> for 3DS lacks rlimit; add a stub that reports nothing.
#pragma once
#include_next <sys/resource.h>
#ifndef RLIMIT_STACK
#define RLIMIT_STACK 3
typedef unsigned long rlim_t;
struct rlimit {
	rlim_t rlim_cur;
	rlim_t rlim_max;
};
static inline int getrlimit(int r, struct rlimit *l) { (void)r; l->rlim_cur = 0; l->rlim_max = 0; return 0; }
static inline int setrlimit(int r, const struct rlimit *l) { (void)r; (void)l; return -1; }
#endif
