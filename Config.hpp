#pragma once
#include <ostream>
#include <fstream>
#include <streambuf>
#include <string>
#include <cstddef>
#include <regex>

#include "IpAddress.hpp"

namespace IpLink {

/* Using X-macro pattern */

#define X_CONFIG \
		X(uart, string, "/dev/ttyS0", string, string, "Serial device path") \
		X(baud, int, 115200, strtonatural, std::to_string, "Serial baud rate") \
		X(ifname, string, "uart0", string, string, "TUN interface name") \
		X(mtu, int, 115200/32, strtonatural, std::to_string, "Interface MTU") \
		X(addr, ip_address, "10.101.0.1/30", ip_address, std::to_string, "Local IP address") \
		X(keepalive_interval, int, 500, strtonatural, std::to_string, "Keep-alive interval in milliseconds") \
		X(keepalive_limit, int, 3, strtonatural, std::to_string, "Number of missed keep-alive messages before assuming peer has disconnected") \
		X(updown, bool, false, strtobool, booltostr, "Set TUN up/down in response to peer connection/disconnection") \
		X(verbose, bool, false, strtobool, booltostr, "Enable extra logging")

class Config
{
	using string = std::string;

public:

	struct parse_error :
		std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

#define X(name, type, def, parse, format, help) type name = type(def);
	X_CONFIG;
#undef X

	Config() = default;

	void set(const std::string& key, const std::string& value);
	void parse_args(int argc, char *argv[], std::ostream& os);
	void parse_config(const std::string& config);

	static void dump_var(std::ostream& os, const char *name, const char *type, const char *def, const char *help, const std::string& value, bool with_help);
	void dump(std::ostream& os, bool with_help) const;

	void help(std::ostream& os);
	bool shown_help = false;
};

#if ! defined KEEP_X_CONFIG
#undef X_CONFIG
#endif

}
