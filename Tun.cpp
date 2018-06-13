#include <net/if.h>

extern "C" {
#include "if.h"
#include "tun.h"
}

#include "Tun.hpp"

namespace Linux {

static int tun_fd_init(const std::string& name, char out[IFNAMSIZ + 1], Flags flags)
{
	int fd = tun_fd(name.c_str(), out, detail::translate_flags(flags, O_NONBLOCK, O_CLOEXEC));
	if (fd == -1) {
		out[0] = 0;
	}
	return fd;
}

Tun::Tun(const std::string& name, Flags flags) :
	FileDescriptor(tun_fd_init(name, this->name, flags), "tun_fd"),
	mtu(1280)
{
}

Tun::~Tun()
{
	if (get_fd() != -1) {
		set_up(false);
	}
}

Tun::Frame Tun::recv()
{
	const size_t bufsize = sizeof(struct tun_frame_info) + mtu;
	Frame frame(bufsize);
	frame.size = read(frame.buffer, frame.size);
	return frame;
}

void Tun::send(const Frame& frame)
{
	write(frame.buffer, frame.size);
}

const std::string Tun::get_name() const
{
	return static_cast<const char *>(name);
}

void Tun::set_point_to_point(bool value)
{
	if (if_set_flags(name, IFF_POINTOPOINT, value) < 0) {
		throw SystemError("Failed to configure interface point-to-point flag");
	}
}

void Tun::set_mtu(std::size_t mtu)
{
	if (if_set_mtu(name, mtu) < 0) {
		throw SystemError("Failed to configure interface MTU");
	}
}

void Tun::set_up(bool value)
{
	if (if_set_up(name, value) < 0) {
		throw SystemError("Failed to set link up/down");
	}
}

void Tun::set_addr(const std::string& addr, const std::string& mask)
{
	if (if_set_addr(name, addr.c_str(), mask.c_str()) < 0) {
		throw SystemError("Failed to set link address/mask");
	}
}

void Tun::set_route(const std::string& gateway, int metric, const std::string& remote_addr, const std::string& remote_mask)
{
	if (if_set_route(name, gateway.c_str(), metric, remote_addr.c_str(), remote_mask.c_str()) < 0) {
		throw SystemError("Failed to set route/gateway address/mask");
	}
}

}
