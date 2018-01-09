#pragma once
#include <stddef.h>
#include <stdbool.h>

/* Set MTU of interface */
int if_set_mtu(const char *ifname, size_t mtu);

/* Get interface flags */
int if_get_flags(const char *ifname, int flags);

/* Set interface flags */
int if_set_flags(const char *ifname, int flags, bool set);

/* Bring interface up/down */
int if_set_up(const char *ifname, bool up);

/* Add address to interface */
int if_set_addr(const char *ifname, const char *addr, const char *mask);

/* Set route for interface */
int if_set_route(const char *ifname, const char *gateway, int metric, const char *target_addr, const char *target_mask);
