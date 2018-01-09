#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>

#include <sys/ioctl.h>

#include <net/route.h>

#include <arpa/inet.h>

#include <linux/if.h>

#include "tun.h"

/* Set MTU of interface */
int if_set_mtu(const char *ifname, size_t mtu)
{
	int ret = 0;

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		goto fail;
	}

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	ifr.ifr_addr.sa_family = AF_INET;
	ifr.ifr_mtu = mtu;

	if (ioctl(fd, SIOCSIFMTU, (void *) &ifr) < 0) {
		perror("ioctl(SIOCSIFMTU)");
		goto fail;
	}

	goto done;
fail:
	ret = -1;
done:
	close(fd);
	return ret;
}

/* Open a dummy socket, for use with certain ioctls */
static int if_dummy_open()
{
	int ret = 0;

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		goto fail;
	}

	ret = fd;

	goto done;
fail:
	ret = -1;
done:
	return ret;
}

/* Get interface flags */
int if_get_flags(const char *ifname, int flags)
{
	int ret = 0;

	int fd = if_dummy_open();
	if (fd < 0) {
		perror("if_dummy_open");
		goto fail;
	}

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	if (ioctl(fd, SIOCGIFFLAGS, (void *) &ifr) < 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		goto fail;
	}

	ret = ifr.ifr_flags & flags;

	goto done;
fail:
	ret = -1;
done:
	close(fd);
	return ret;
}

/* Set interface flags */
int if_set_flags(const char *ifname, int flags, bool set)
{
	int ret = 0;

	int fd = if_dummy_open();
	if (fd < 0) {
		perror("if_dummy_open");
		goto fail;
	}

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	if (ioctl(fd, SIOCGIFFLAGS, (void *) &ifr) < 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		goto fail;
	}

	if (set) {
		ifr.ifr_flags |= flags;
	} else {
		ifr.ifr_flags &= ~flags;
	}

	if (ioctl(fd, SIOCSIFFLAGS, (void *) &ifr) < 0) {
		perror("ioctl(SIOCSIFFLAGS)");
		goto fail;
	}

	goto done;
fail:
	ret = -1;
done:
	close(fd);
	return ret;
}

/* Bring interface up/down */
int if_set_up(const char *ifname, bool up)
{
	return if_set_flags(ifname, IFF_UP, up);
}

/* Add address to interface */
int if_set_addr(const char *ifname, const char *addr, const char *mask)
{
	int ret = 0;

	int fd = -1;

	struct sockaddr_in sai_addr;
	memset(&sai_addr, 0, sizeof(sai_addr));
	sai_addr.sin_family = AF_INET;
	if (!inet_aton(addr, &sai_addr.sin_addr)) {
		perror("inet_aton");
		goto fail;
	}

	struct sockaddr_in sai_mask;
	memset(&sai_mask, 0, sizeof(sai_mask));
	sai_mask.sin_family = AF_INET;
	if (!inet_aton(mask, &sai_mask.sin_addr)) {
		perror("inet_aton");
		goto fail;
	}

	fd = if_dummy_open(ifname);
	if (fd < 0) {
		perror("if_dummy_open");
		goto fail;
	}

	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	memcpy(&ifr.ifr_addr, &sai_addr, sizeof(sai_addr));

	if (ioctl(fd, SIOCSIFADDR, (void *) &ifr) < 0) {
		perror("ioctl(SIOCSIFADDR)");
		goto fail;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	memcpy(&ifr.ifr_netmask, &sai_mask, sizeof(sai_mask));

	if (ioctl(fd, SIOCSIFNETMASK, (void *) &ifr) < 0) {
		perror("ioctl(SIOCSIFNETMASK)");
		goto fail;
	}

	goto done;
fail:
	ret = -1;
done:
	close(fd);
	return ret;
}

/* Set route for interface */
int if_set_route(const char *ifname, const char *gateway, int metric, const char *target_addr, const char *target_mask)
{
	int ret = 0;

	int fd = -1;

	struct sockaddr_in sai_gate;
	sai_gate.sin_family = AF_INET;
	if (!inet_aton(gateway, &sai_gate.sin_addr)) {
		perror("inet_aton");
		goto fail;
	}

	struct sockaddr_in sai_target_addr;
	sai_target_addr.sin_family = AF_INET;
	if (!inet_aton(target_addr, &sai_target_addr.sin_addr)) {
		perror("inet_aton");
		goto fail;
	}

	struct sockaddr_in sai_target_mask;
	sai_target_mask.sin_family = AF_INET;
	if (!inet_aton(target_mask, &sai_target_mask.sin_addr)) {
		perror("inet_aton");
		goto fail;
	}

	struct rtentry rte;
	memset(&rte, 0, sizeof(rte));
	memcpy(&rte.rt_gateway, &sai_gate, sizeof(sai_gate));
	memcpy(&rte.rt_dst, &sai_target_addr, sizeof(sai_target_addr));
	memcpy(&rte.rt_genmask, &sai_target_mask, sizeof(sai_target_mask));
	rte.rt_flags = RTF_UP | RTF_GATEWAY;
	rte.rt_metric = metric;

	fd = if_dummy_open(ifname);
	if (fd < 0) {
		perror("if_dummy_open");
		goto fail;
	}

	if (ioctl(fd, SIOCADDRT, (void *) &rte) < 0) {
		perror("ioctl(SIOCADDRT)");
		goto fail;
	}

	goto done;
fail:
	ret = -1;
done:
	close(fd);
	return ret;
}
