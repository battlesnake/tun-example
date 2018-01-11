#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
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
#include "udp.h"
#include "reactor.h"
#include "config.h"

static int init_tun(char *ifname)
{
	int ret = 0;

	strncpy(ifname, tun_if_name, IFNAMSIZ);

	int tun = tun_fd(ifname, ifname);
	if (tun	< 0) {
		perror("tun_fd");
		goto fail;
	}

	if (if_set_flags(ifname, IFF_POINTOPOINT, true) < 0) {
		perror("if_set_flags");
		goto fail;
	}

	if (if_set_mtu(ifname, tun_if_mtu) < 0) {
		perror("if_set_mtu");
		goto fail;
	}

	if (if_set_up(ifname, true) < 0) {
		perror("if_set_up");
		goto fail;
	}

	if (if_set_addr(ifname, tun_ip_local, tun_ip_mask) < 0) {
		perror("if_set_addr");
		goto fail;
	}

	if (if_set_route(ifname, tun_ip_remote, route_metric, route_addr, route_mask) < 0) {
		perror("if_set_route");
		goto fail;
	}

	goto done;
fail:
	ret = -1;
done:
	return ret;
}

static void print_config()
{
	printf("TUN configuration\n");
	printf(" * %s = %s\n", "interface name", tun_if_name);
	printf(" * %s = %zu\n", "interface MTU", tun_if_mtu);
	printf(" * %s = %s\n", "local address", tun_ip_local);
	printf(" * %s = %s\n", "remote address", tun_ip_remote);
	printf(" * %s = %s\n", "network mask", tun_ip_mask);
	printf("\n");
	printf("Route configuration\n");
	printf(" * %s = %s\n", "route address", route_addr);
	printf(" * %s = %s\n", "route mask", route_mask);
	printf(" * %s = %d\n", "route metric", route_metric);
	printf("\n");
	printf("Proxy configuration\n");
	printf(" * %s = %s\n", "local address", udp_ip_local);
	printf(" * %s = %s\n", "remote mask", udp_ip_remote);
	printf(" * %s = %" PRIu16 "\n", "port", udp_port);
	printf(" * %s = %s\n", "network mask", udp_ip_mask);
	printf("\n");
}

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;
	int ret = -1;

	if (getuid() != 0 && !capng_have_capability(CAPNG_EFFECTIVE, CAP_NET_ADMIN)) {
		error("Requires root or CAP_NET_ADMIN privilege");
		return EACCES;
	}

	print_config();

	char ifname[IFNAMSIZ];

	int tun = init_tun(ifname);
	if (tun == -1) {
		goto fail1;
	}

	int udp = udp_fd(udp_ip_remote, udp_port);
	if (udp == -1) {
		goto fail2;
	}

	if (event_loop(tun, tun_if_mtu) < 0) {
		perror("event_loop");
		goto fail3;
	}

	ret = 0;

fail3:
	if (shutdown(udp, SHUT_RDWR)) {
		perror("shutdown");
	}
	if (close(udp)) {
		perror("close");
	}

fail2:
	if (if_set_up(ifname, false) < 0) {
		perror("if_set_down");
	}
	if (close(tun)) {
		perror("close");
	}

fail1:
	return ret;
}
