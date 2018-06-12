#include <algorithm>
#include <cstring>
#include <iostream>

#include "iplink_address.hpp"

using namespace std;

static int str(const char *begin, const char *end)
{
	if (end <= begin) {
		return -1;
	}
	std::string s(begin, end);
	char *ep;
	long ret = strtoul(s.c_str(), &ep, 10);
	if (s.empty() || *ep || ret < 0 || ret > 255) {
		return -1;
	}
	return ret;
}

ip_address::ip_address(const char *s, std::size_t len)
{
	if (len == 0) {
		len = strlen(s);
	}
	const char *begin = s;
	const char *end = s + len;
	auto d0 = find(begin, end, '.');
	auto d1 = find(d0 + 1, end, '.');
	auto d2 = find(d1 + 1, end, '.');
	auto d3 = find(d2 + 1, end, '/');
	int b0 = str(begin, d0);
	int b1 = str(d0 + 1, d1);
	int b2 = str(d1 + 1, d2);
	int b3 = str(d2 + 1, d3);
	int b4 = d3 == end ? 32 : str(d3 + 1, end);
	if (b0 == -1 || b1 == -1 || b2 == -1 || b3 == -1 || b4 == -1 || b4 > 32) {
		throw parse_error("Unable to parse IP address: " + string(begin, end));
	}
	addr[0] = b0;
	addr[1] = b1;
	addr[2] = b2;
	addr[3] = b3;
	length = b4;
}

static std::string ipv4str(std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint8_t b3)
{
	return to_string(b0) + '.'
			+ to_string(b1) + '.'
			+ to_string(b2) + '.'
			+ to_string(b3);
}

std::string ip_address::get_address() const
{
	return ipv4str(addr[0], addr[1], addr[2], addr[3]);
}

std::string ip_address::get_mask() const
{
	std::uint32_t mask = 0xffffffffUL;
	mask <<= 32 - length;
	return ipv4str(mask >> 24, mask >> 16, mask >> 8, mask);
}

ip_address::operator std::string() const
{
	return get_address() + '/' + to_string(length);
}
