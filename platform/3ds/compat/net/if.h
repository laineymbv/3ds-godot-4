// Minimal <net/if.h> for the 3DS (one Wi-Fi interface, no index lookup).
#pragma once
#ifndef IFF_UP
#define IFF_UP 0x1
#endif
#define IFF_LOOPBACK 0x8
#define IF_NAMESIZE 16
static inline unsigned int if_nametoindex(const char *p_name) {
	(void)p_name;
	return 0;
}
