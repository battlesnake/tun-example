#pragma once
#include <iostream>
#include <string>
#include <cstddef>

namespace IpLink {

/* Using X-macro pattern */

#define X_STATS \
		X(uart_rx_bytes) \
		X(uart_tx_bytes) \
		\
		X(tun_rx_bytes) \
		X(tun_tx_bytes) \
		X(tun_rx_ignored_bytes) \
		\
		X(tun_rx_frames) \
		X(tun_tx_frames) \
		X(tun_rx_ignored_frames)

class Stats
{

#define X(name) std::size_t name = 0;
	X_STATS;
#undef X

public:

	void print(std::ostream& os)
	{
#define X(name) os << "\t" << #name << ": " << name << std::endl;
		X_STATS;
#undef X
		os << std::endl;
	}

#define X(name) void inc_##name(std::size_t amount) { name += amount; }
	X_STATS;
#undef X

};

#undef X_STATS

}
