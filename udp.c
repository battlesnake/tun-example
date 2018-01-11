#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "udp.h"

int udp_fd(const char *addr, uint16_t port)
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd == -1) {
		perror("socket");
		return -1;
	}

	struct sockaddr_in sai;
	memset(&sai, 0, sizeof(sai));

	sai.sin_family = AF_INET;
	sai.sin_addr.s_addr = inet_addr(addr);
	sai.sin_port = htons(port);

	if (inet_pton(AF_INET, addr, &sai.sin_addr) != 1) {
		perror("inet_pton");
		return -1;
	}

	if (bind(fd, (struct sockaddr *) &sai, sizeof(sai)) == -1) {
		perror("bind");
		return -1;
	}

	return fd;
}
