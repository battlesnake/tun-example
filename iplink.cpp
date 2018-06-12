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

#include "iplink_config.hpp"
#include "iplink_stats.hpp"

extern "C" {
#include "hexdump.h"
}

/*** Program ***/

int main(int argc, char *argv[])
{
	(void) argv;
	(void) argc;

	Config config;
	config.parse_args(argc, argv, std::cout);

	if (config.shown_help) {
		return 0;
	}

#if defined SUPPORT_CAP
	if (getuid() != 0 && !capng_have_capability(CAPNG_EFFECTIVE, CAP_NET_ADMIN)) {
		throw std::runtime_error("Requires root or CAP_NET_ADMIN privilege");
	}
#endif

	Linux::SignalSet ss;
	ss.add(Linux::sig_int);
	ss.add(Linux::sig_term);
	ss.add(Linux::sig_quit);
	ss.add(Linux::sig_usr1);

	const Linux::Flags flags = Linux::close_on_exec | Linux::non_blocking;
	Linux::SignalFD sfd(ss, true, flags);
	Linux::Serial uart(config.uart, config.baud, flags);
	Linux::Tun tun(config.ifname, flags);

	tun.set_point_to_point(true);
	tun.set_mtu(config.mtu);
	tun.set_addr(config.addr.get_address(), config.addr.get_mask());
	// tun.set_route(remote_addr, 1, remote_addr, link_mask);
	tun.set_up(true);

	Stats stats;

	bool terminating = false;

	std::list<std::vector<std::uint8_t>> uart_rx_buf;
	std::deque<std::uint8_t> uart_tx_buf;

	/* Used for receiving blocks from serial */
	std::vector<std::uint8_t> buffer;

	Kiss::Encoder encoder;
	Kiss::Decoder decoder(config.mtu + sizeof(struct tun_frame_info));

	Linux::EpollFD epfd(Linux::close_on_exec);

	using Events = Linux::EpollFD::Events;
	using Frame = Linux::Tun::Frame;

	const auto verbose_hexdump = [&] (const char *title, const void *buf, size_t len) {
		if (config.verbose) {
			hexdump(title, buf, len);
		}
	};

	const auto update_serial = [&] () {
		epfd.rebind(uart,
			(uart_rx_buf.empty() ? Events::event_in : Events::event_none) |
			(!uart_tx_buf.empty() ? Events::event_out : Events::event_none));
	};

	const auto update_tun = [&] () {
		epfd.rebind(tun,
			(uart_tx_buf.empty() ? Events::event_in : Events::event_none) |
			(!uart_rx_buf.empty() ? Events::event_out : Events::event_none));
	};

	const auto on_signal = [&] (Events events) {
		if (events & Events::event_in) {
			const auto ssi = sfd.take_signal();
			switch (ssi.ssi_signo) {
			case SIGINT:
			case SIGTERM:
			case SIGQUIT:
				terminating = true;
				break;
			case SIGUSR1:
				stats.print(std::cout);
				break;
			}
		}
	};

	const auto on_serial = [&] (Events events) {
		if (events & Events::event_in) {
			buffer.resize(1 << 16);
			uart.read(buffer);
			stats.inc_uart_rx_bytes(buffer.size());
			uart_rx_buf.splice(uart_rx_buf.end(), decoder.decode(buffer));
		}
		if (events & Events::event_out) {
			const auto block_size = std::min<std::size_t>(1 << 16, uart_tx_buf.size());
			buffer.resize(block_size);
			const auto block_begin = uart_tx_buf.begin();
			const auto block_end = block_begin + block_size;
			std::copy(block_begin, block_end, buffer.begin());
			const auto sent_length = uart.write(buffer.data(), buffer.size());
			stats.inc_uart_tx_bytes(sent_length);
			const auto sent_end = block_begin + sent_length;
			uart_tx_buf.erase(block_begin, sent_end);
		}
		update_tun();
		update_serial();
	};

	const auto on_packet = [&] (Events events) {
		if (events & Events::event_in) {
			const auto frame = tun.recv();
			stats.inc_tun_rx_frames(1);
			const auto begin = reinterpret_cast<const std::uint8_t *>(frame.buffer);
			const auto end = begin + frame.size;
			const auto buf = encoder.encode_packet(begin, end);
			std::copy(buf.cbegin(), buf.cend(), std::back_inserter(uart_tx_buf));
			verbose_hexdump("<", frame.buffer, frame.size);
		}
		if (events & Events::event_out) {
			const auto buf = std::move(uart_rx_buf.front());
			uart_rx_buf.pop_front();
			Frame frame(static_cast<void *>(const_cast<std::uint8_t *>(buf.data())), buf.size());
			tun.send(frame);
			stats.inc_tun_tx_frames(1);
			verbose_hexdump(">", frame.buffer, frame.size);
		}
		update_tun();
		update_serial();
	};

	epfd.bind(sfd, on_signal, Events::event_in);
	epfd.bind(uart, on_serial, Events::event_in);
	epfd.bind(tun, on_packet, Events::event_in);

	while (!terminating) {
		epfd.wait();
	}
}
