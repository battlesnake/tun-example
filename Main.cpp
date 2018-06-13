#include <getopt.h>
#include <unistd.h>

#if defined SUPPORT_CAP
#include <cap-ng.h>
#endif

#include "IpLink.hpp"

/*** Program ***/

int main(int argc, char *argv[])
{
	IpLink::Config config;
	config.parse_args(argc, argv, std::cout);

	if (config.shown_help) {
		return 0;
	}

	if (config.daemon) {
		pid_t p = fork();
		if (p == -1) {
			throw std::runtime_error(std::string("fork() failed: ") + strerror(errno));
		} else if (p > 0) {
			usleep(100000);
			int status;
			if (waitpid(p, &status, WNOHANG) == p && (WIFEXITED(status) || WIFSIGNALED(status))) {
				return status;
			}
			return 0;
		}
	}

#if defined SUPPORT_CAP
	if (getuid() != 0 && !capng_have_capability(CAPNG_EFFECTIVE, CAP_NET_ADMIN)) {
		throw std::runtime_error("Requires root or CAP_NET_ADMIN privilege");
	}
#endif

	IpLink::IpLink iplink(config);

	iplink.run();

	return 0;
}
