#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#include <sys/ioctl.h>
#include <sys/signalfd.h>

#include <net/route.h>
#include <netinet/in.h>

#include <arpa/inet.h>

#include <linux/if.h>
#include <linux/if_tun.h>

#include <cap-ng.h>

#include "log.h"
#include "if.h"
#include "tun.h"
#include "reactor.h"

/* Configuration */
static const char if_name[] = "demo-tun";
static const char ip_local[] = "10.11.19.6";
static const char ip_remote[] = "10.11.19.5";
static const char ip_mask[] = "255.255.255.252";
static const char outside_addr[] = "0.0.0.0";
static const char outside_mask[] = "0.0.0.0";
static const size_t mtu = 4000;
static const int metric = 10000;

/* Entry point */
int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;
	int ret = 0;

	if (getuid() != 0 && !capng_have_capability(CAPNG_EFFECTIVE, CAP_NET_ADMIN)) {
		error("Requires root or CAP_NET_ADMIN privilege");
		return EACCES;
	}


	/* Print configuration */
	printf("TUN configuration\n");
	printf(" * %s = %s\n", "interface name", if_name);
	printf(" * %s = %s\n", "local address", ip_local);
	printf(" * %s = %s\n", "remote address", ip_remote);
	printf(" * %s = %s\n", "network mask", ip_mask);
	printf(" * %s = %s\n", "world address", outside_addr);
	printf(" * %s = %s\n", "world mask", outside_mask);
	printf(" * %s = %zu\n", "MTU", mtu);
	printf(" * %s = %d\n", "metric", metric);

	/* Initialise */
	char ifname[IFNAMSIZ];
	strncpy(ifname, if_name, IFNAMSIZ);

	int tun = tun_fd(ifname, ifname);
	if (tun	< 0) {
		perror("tun_fd");
		goto fail;
	}

	if (if_set_flags(ifname, IFF_POINTOPOINT, true) < 0) {
		perror("if_set_flags");
		goto fail;
	}

	if (if_set_mtu(ifname, mtu) < 0) {
		perror("if_set_mtu");
		goto fail;
	}

	if (if_set_up(ifname, true) < 0) {
		perror("if_set_up");
		goto fail;
	}

	if (if_set_addr(ifname, ip_local, ip_mask) < 0) {
		perror("if_set_addr");
		goto fail;
	}

	if (if_set_route(ifname, ip_remote, metric, outside_addr, outside_mask) < 0) {
		perror("if_set_route");
		goto fail;
	}

	/* Run event loop */
	if (event_loop(tun, mtu) < 0) {
		perror("event_loop");
		goto fail;
	}

	/* Bring interface down, clean up */
	if (if_set_up(ifname, false) < 0) {
		perror("if_set_down");
		goto fail;
	}

	goto done;
fail:
	ret = -1;
done:
	close(tun);
	return ret;
}
