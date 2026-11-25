// libctru has no getifaddrs(); report the console's one Wi-Fi address
// (gethostid() returns it once soc:u is up).
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IFF_UP
#define IFF_UP 0x1
#endif

struct ifaddrs {
	struct ifaddrs *ifa_next;
	char *ifa_name;
	unsigned int ifa_flags;
	struct sockaddr *ifa_addr;
	struct sockaddr *ifa_netmask;
	struct sockaddr *ifa_dstaddr;
	void *ifa_data;
};

static inline int getifaddrs(struct ifaddrs **r_ifap) {
	long id = gethostid();
	if (id == 0 || id == -1) {
		*r_ifap = NULL;
		return -1;
	}
	struct {
		struct ifaddrs ifa;
		struct sockaddr_in addr;
		char name[8];
	} *blk = (decltype(blk))calloc(1, sizeof(*blk));
	if (!blk) {
		return -1;
	}
	blk->addr.sin_family = AF_INET;
	blk->addr.sin_addr.s_addr = (in_addr_t)id;
	strcpy(blk->name, "wlan0");
	blk->ifa.ifa_name = blk->name;
	blk->ifa.ifa_flags = IFF_UP;
	blk->ifa.ifa_addr = (struct sockaddr *)&blk->addr;
	*r_ifap = &blk->ifa;
	return 0;
}

static inline void freeifaddrs(struct ifaddrs *p_ifa) {
	free(p_ifa);
}
