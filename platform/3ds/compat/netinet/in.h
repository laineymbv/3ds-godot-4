// libctru's sockets are IPv4 only. These IPv6 types let Godot's shared
// socket code compile; creating an AF_INET6 socket fails at run time and
// Godot uses IPv4 for IPv4 addresses.
#pragma once
#include_next <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#define IPV6_V6ONLY 26
#define IPV6_ADD_MEMBERSHIP 20
#define IPV6_DROP_MEMBERSHIP 21

struct in6_addr {
	uint8_t s6_addr[16];
};

struct sockaddr_in6 {
	sa_family_t sin6_family;
	in_port_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

struct ipv6_mreq {
	struct in6_addr ipv6mr_multiaddr;
	unsigned int ipv6mr_interface;
};

static const struct in6_addr in6addr_any = { { 0 } };
#endif
