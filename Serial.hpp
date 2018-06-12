#pragma once
#include "Linux.hpp"

namespace Linux {

struct Serial :
	File,
	ReadableFileDescriptor,
	WritableFileDescriptor
{
	Serial(const std::string& path, int baud, Flags flags = Flags::none);
};

}
