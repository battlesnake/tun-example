#pragma once
#include <string>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace IpLink {

/* IPv4 only, extend to IPv6 in future */
class ip_address
{
	std::array<std::uint8_t, 4> addr;
	int length;
public:
	struct parse_error :
		std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};
	ip_address(const ip_address&) = default;
	ip_address& operator = (const ip_address&) = default;
	explicit ip_address(const std::string& s) : ip_address(s.c_str(), s.length()) { }
	explicit ip_address(const char *s, std::size_t len = 0);
	std::string get_address() const;
	std::string get_mask() const;
	operator std::string() const;
};

}

namespace std {
	inline string to_string(const IpLink::ip_address& addr) { return addr; }
}
