#include <getopt.h>

#if defined SUPPORT_CAP
#include <cap-ng.h>
#endif

#include <iostream>

#include <list>
#include <deque>
#include <vector>

#include "Linux.hpp"
#include "Serial.hpp"
#include "Tun.hpp"

#include "Kiss.hpp"

#include "IpLink.hpp"

extern "C" {
#include "hexdump.h"
}

/*** Program ***/

int main(int argc, char *argv[])
{
	IpLink::Config config;
	config.parse_args(argc, argv, std::cout);

	if (config.shown_help) {
		return 0;
	}

#if defined SUPPORT_CAP
	if (getuid() != 0 && !capng_have_capability(CAPNG_EFFECTIVE, CAP_NET_ADMIN)) {
		throw std::runtime_error("Requires root or CAP_NET_ADMIN privilege");
	}
#endif

	IpLink::IpLink iplink(config);

	iplink.run();
}
