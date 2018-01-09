#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <linux/if.h>
#include <linux/if_tun.h>

#include "log.h"
#include "tun.h"

/* Initialise a TUN instance */
int tun_fd(const char *dev, char *dev_out)
{
	int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		return -1;
	}

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN;
	if (dev) {
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
		perror("ioctl(TUNSETIFF)");
		goto fail;
	}

	if (dev_out) {
		strncpy(dev_out, ifr.ifr_name, IFNAMSIZ);
	}

	goto done;

fail:
	close(fd);
	fd = -1;
done:
	return fd;
}
