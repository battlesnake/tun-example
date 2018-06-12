#include <unordered_map>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "Linux.hpp"
#include "Serial.hpp"

namespace Linux {

#define X(b) { b, B##b }
static const std::unordered_map<int, int> baud_constants = {
	X(50),
	X(75),
	X(110),
	X(134),
	X(150),
	X(200),
	X(300),
	X(600),
	X(1200),
	X(1800),
	X(2400),
	X(4800),
	X(9600),
	X(19200),
	X(38400),
	X(57600),
	X(115200),
	X(230400),
	X(460800),
	X(500000),
	X(921600),
	X(1000000),
	X(1152000),
	X(1500000),
	X(2000000),
	X(2500000),
	X(3000000),
	X(3500000),
	X(4000000),
};
#undef X

Serial::Serial(const std::string& path, int baud, Flags flags) :
	File(path, file_read_write, file_none, flags)
{
	const int fd = get_fd();
	const auto baud_it = baud_constants.find(baud);
	if (baud_it == baud_constants.end()) {
		throw SystemError("Unsupported baud rate: " + std::to_string(baud));
	}
	/* Get current UART configuration */
	struct termios t;
	if (tcgetattr(fd, &t) < 0) {
		throw SystemError("tcgetattr failed");
	}
	/* Set baud in config structure */
	if (cfsetspeed(&t, baud_it->second) < 0) {
		throw SystemError("cfsetspeed failed");
	}
	/* Other configuration (no stop bit, no flow control) */
	t.c_cflag &= ~(CSTOPB | CRTSCTS);
	cfmakeraw(&t);
	/* Re-configure interface */
	if (tcsetattr(fd, TCSANOW, &t) < 0) {
		throw SystemError("tcsetattr failed");
	}
	/* Flush buffer */
	if (tcflush(fd, TCIOFLUSH) < 0) {
		throw SystemError("tcflush failed");
	}
}

}
