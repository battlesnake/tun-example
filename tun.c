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

/* Configuration */
static const char if_name[] = "demo-tun";
static const char ip_local[] = "10.109.107.99";
static const char ip_remote[] = "10.109.107.98";
static const char ip_mask[] = "255.255.255.254";
static const char outside_addr[] = "0.0.0.0";
static const char outside_mask[] = "0.0.0.0";
static const size_t mtu = 4000;
static const int metric = 10000;

/* Logging macros */
#define log(fmt, ...) fprintf(stderr, /*"%-8.8s:%4d: "*/ fmt "\n", /*__FILE__, __LINE__,*/ ##__VA_ARGS__)
#define error(fmt, ...) log("\x1b[1;31m [fail] " fmt "\x1b[0m", ##__VA_ARGS__)
#define warn(fmt, ...) log("\x1b[1;33m [warn] " fmt "\x1b[0m", ##__VA_ARGS__)
#define info(fmt, ...) log("\x1b[1;36m [info] " fmt "\x1b[0m", ##__VA_ARGS__)

/* Class for TUN device */
struct tun {
	int fd;
};

/* Initialise a TUN instance */
int tun_init(struct tun *self, const char *dev, char *dev_out)
{
	int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		return -1;
	}
	self->fd = fd;

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN;
	if (dev) {
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
		perror("ioctl(TUNSETIFF)");
		return -1;
	}

	if (dev_out) {
		strncpy(dev_out, ifr.ifr_name, IFNAMSIZ);
	}

	return 0;
}

/* Destroy a TUN instance */
int tun_free(struct tun *self)
{
	close(self->fd);
	return 0;
}

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
int if_dummy_open()
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

/* Main event-loop */
int event_loop(struct tun *tun, size_t mtu)
{
	int ret = 0;

	sigset_t ss;
	sigset_t oss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGTERM);
	sigaddset(&ss, SIGINT);
	if (sigprocmask(SIG_BLOCK, &ss, &oss) < 0) {
		perror("sigprocmask");
		return -1;
	}

	int sfd = signalfd(-1, &ss, SFD_NONBLOCK);
	if (sfd < 0) {
		perror("signalfd");
		goto fail;
	}

	const int isig = 0;
	const int itun = 1;
	struct pollfd pfd[2];
	memset(pfd, 0, sizeof(pfd));

	/* SIGTERM/SIGINT events cause program to end */
	pfd[isig].fd = sfd;
	pfd[isig].events = POLLIN;

	/* Input events on TUN device */
	pfd[itun].fd = tun->fd;
	pfd[itun].events = POLLIN;

	/* Structures to represent data read from TUN */
	struct __attribute__((__packed__)) tun_frame_info
	{
		uint16_t flags;
		uint16_t proto;
	};

	struct __attribute__((__packed__)) tun_frame
	{
		struct tun_frame_info info;
		uint8_t data[];
	};


	while (!(pfd[isig].revents & POLLIN)) {
		if (poll(pfd, sizeof(pfd) / sizeof(pfd[0]), -1) < 0) {
			perror("poll");
			goto fail;
		}
		if (pfd[itun].revents & POLLIN) {
			union {
				char buf[sizeof(struct tun_frame_info) + mtu];
				struct tun_frame frame;
			} wrap;
			ssize_t len = read(tun->fd, &wrap, sizeof(wrap));
			if (len < 0) {
				perror("read");
				goto fail;
			}
			len -= sizeof(wrap.frame.info);
			info("tun: proto=0x%04hx flags=0x%04x bytes=%zu", wrap.frame.info.proto, wrap.frame.info.flags, len);
		}
	}

	struct signalfd_siginfo ssi;

	if (read(sfd, &ssi, sizeof(ssi)) < 0) {
		perror("read");
		goto fail;
	}

	log("Stopped by signal: %s", strsignal(ssi.ssi_signo));

	goto done;
fail:
	ret = -1;
done:
	close(sfd);
	if (sigprocmask(SIG_SETMASK, &oss, NULL) < 0) {
		perror("sigprocmask");
	}
	return ret;
}

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

	struct tun tun;
	if (tun_init(&tun, ifname, ifname) < 0) {
		perror("tun_init");
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
	if (event_loop(&tun, mtu) < 0) {
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
	tun_free(&tun);
	return ret;
}
