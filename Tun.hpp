#pragma once
#include <memory>
#include <utility>

#include <linux/if.h>

extern "C" {
#include "tun.h"
}

#include "Linux.hpp"

namespace Linux {

struct Tun :
	Linux::FileDescriptor,
	private Linux::ReadableFileDescriptor,
	private Linux::WritableFileDescriptor
{

	struct Frame
	{
		bool owns;
		void *buffer;
		size_t size;
		Frame() : owns(true), buffer(nullptr), size(0) { }
		Frame(size_t size) : owns(true), buffer(malloc(size)), size(size) { }
		Frame(void *buffer, size_t size) : owns(false), buffer(buffer), size(size) { }
		~Frame() { if (owns) { free(buffer); } }
	};

	Tun(const std::string& name, Flags flags = Flags::none);
	~Tun();

	Tun::Frame recv();
	void send(const Frame& frame);

	const std::string get_name() const;

	void set_point_to_point(bool value);
	void set_mtu(std::size_t mtu);
	void set_up(bool value);
	void set_addr(const std::string& addr, const std::string& mask);
	void set_route(const std::string& gateway, int metric, const std::string& remote_addr, const std::string& remote_mask);

private:
	char name[IFNAMSIZ + 1];
	std::size_t mtu;
};

}
