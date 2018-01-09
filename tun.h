#pragma once
#include <stdint.h>

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

/* Initialise a TUN instance */
int tun_fd(const char *dev, char *dev_out);
