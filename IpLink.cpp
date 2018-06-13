extern "C" {
#include "hexdump.h"
#include "checksum.h"
}

#include <iostream>
#include <iomanip>

#include <arpa/inet.h>

#include "format_si.hpp"

#include "IpLink.hpp"

namespace IpLink {

using namespace Linux;

/* Frame types */
static constexpr std::uint8_t ft_keepalive = 0x01;
static constexpr std::uint8_t ft_ip_packet = 0x02;

void IpLink::verbose_hexdump(const char *title, const void *buf, size_t len)
{
	if (config.verbose) {
		hexdump(title, buf, len);
	}
}

void IpLink::update_meter()
{
	auto rx_total = stats.get_uart_rx_bytes();
	auto tx_total = stats.get_uart_tx_bytes();
	rx_meter.write(rx_total);
	tx_meter.write(tx_total);
	if (rx_meter.size() < 2 || tx_meter.size() < 2) {
		return;
	}
	auto rx_rate = rx_meter.rate();
	auto tx_rate = tx_meter.rate();
	std::cerr << "\r\x1b[K";
	std::cerr << "  [rx:" << format_si(rx_total, "B", 3) << " @ " << format_si(rx_rate, "B/s", 3) << "]";
	std::cerr << "  [tx:" << format_si(tx_total, "B", 3) << " @ " << format_si(tx_rate, "B/s", 3) << "]";
}

void IpLink::set_tun_updown(bool value)
{
	if (value == tun_up) {
		return;
	}
	tun.set_up(value);
	if (value) {
		std::cout << "[tun up]" << std::endl;
	} else {
		std::cout << "[tun down]" << std::endl;
	}
	tun_up = value;
	rebind_tun_events();
}

void IpLink::peer_state_changed(bool value)
{
	if (value == is_connected) {
		return;
	}
	if (value) {
		std::cout << "[peer connected]" << std::endl;
		is_connected = true;
	} else {
		std::cout << "[peer disconnected]" << std::endl;
		is_connected = false;
		uart_rx_buf.clear();
		uart_tx_buf.clear();
	}
	is_connected = value;
	if (config.updown) {
		set_tun_updown(value);
	}
}

void IpLink::update_timer(Linux::TimerFD& timer, unsigned delay)
{
	if (delay == 0) {
		return;
	}

	constexpr auto billion = 1'000'000'000L;
	constexpr auto million = 1'000'000L;
	constexpr auto thousand = 1'000L;

	Linux::TimerFD::TimeSpec deadline;
	clock_gettime(Linux::Clock::monotonic, &deadline);
	deadline.tv_sec += delay / thousand;
	deadline.tv_nsec += delay % thousand * million;
	if (deadline.tv_nsec >= billion) {
		deadline.tv_nsec -= billion;
		deadline.tv_sec++;
	}

	timer.set_absolute(deadline, true);
}

void IpLink::reset_send_ka_timer()
{
	send_ka.try_read_tick_count();
	update_timer(send_ka, config.keepalive_interval);
}

void IpLink::reset_recv_ka_timer()
{
	recv_ka.try_read_tick_count();
	update_timer(recv_ka, config.keepalive_interval);
}

void IpLink::rebind_serial_events()
{
	epfd.rebind(uart,
		(uart_rx_buf.empty() ? Events::event_in : Events::event_none) |
		(!uart_tx_buf.empty() ? Events::event_out : Events::event_none));
}

void IpLink::rebind_tun_events()
{
	epfd.rebind(tun,
		(tun_up && uart_tx_buf.empty() ? Events::event_in : Events::event_none) |
		(tun_up && !uart_rx_buf.empty() ? Events::event_out : Events::event_none));
}

void IpLink::rebind_events()
{
	rebind_tun_events();
	rebind_serial_events();
}

void IpLink::send_keepalive()
{
	write_packet(ft_keepalive, &ft_keepalive, 1);

	rebind_serial_events();
	on_sent_keepalive();
}

void IpLink::on_sent_keepalive()
{
	reset_send_ka_timer();
	verbose_hexdump("[keepalive]", NULL, 0);
}

void IpLink::on_received_keepalive()
{
	peer_state_changed(true);
	missed_keepalives = 0;
	reset_recv_ka_timer();
}

void IpLink::on_missed_keepalive()
{
	if (missed_keepalives < config.keepalive_limit && ++missed_keepalives == config.keepalive_limit) {
		peer_state_changed(false);
	}
}

void IpLink::on_signal(Events events)
{
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
}

void IpLink::on_update_meter(Events events)
{
	if (events & Events::event_in) {
		meter_timer.read_tick_count();
		update_meter();
	}
}

void IpLink::on_send_ka_timer(Events events)
{
	if (events & Events::event_in) {
		send_ka.read_tick_count();
		send_keepalive();
	}
}

void IpLink::on_recv_ka_timer(Events events)
{
	if (events & Events::event_in) {
		recv_ka.read_tick_count();
		on_missed_keepalive();
		reset_recv_ka_timer();
	}
}

void IpLink::on_serial(Events events)
{
	if (events & Events::event_in) {
		on_serial_readable();
	}
	if (events & Events::event_out) {
		on_serial_writable();
	}
	rebind_events();
}

void IpLink::on_tun(Events events)
{
	if (events & Events::event_in) {
		on_tun_readable();
	}
	if (events & Events::event_out) {
		on_tun_writable();
	}
	rebind_events();
}

void IpLink::on_serial_readable()
{
	buffer.resize(1 << 16);
	uart.read(buffer);
	stats.inc_uart_rx_bytes(buffer.size());
	uart_rx_buf.splice(uart_rx_buf.end(), decoder.decode(buffer));
	on_received_keepalive();
}

void IpLink::on_serial_writable()
{
	/*
	 * Take data from queue and send it, remove sent data
	 * from queue
	 */
	const auto block_size = std::min<std::size_t>(1 << 16, uart_tx_buf.size());
	buffer.resize(block_size);
	const auto block_begin = uart_tx_buf.begin();
	const auto block_end = block_begin + block_size;
	std::copy(block_begin, block_end, buffer.begin());
	const auto sent_length = uart.write(buffer.data(), buffer.size());
	stats.inc_uart_tx_bytes(sent_length);
	const auto sent_end = block_begin + sent_length;
	uart_tx_buf.erase(block_begin, sent_end);
	/* Reset keepalive timer since we've just sent data */
	if (sent_length > 0) {
		on_sent_keepalive();
	}
}

void IpLink::on_tun_readable()
{
	const auto frame = tun.recv();
	if (tun_up) {
		stats.inc_tun_rx_frames(1);
		stats.inc_tun_rx_bytes(frame.size - sizeof(struct tun_frame_info));

		write_packet(ft_ip_packet, frame.buffer, frame.size);

		verbose_hexdump("TUN ==> UART", frame.buffer, frame.size);
	} else {
		stats.inc_tun_rx_ignored_frames(1);
		stats.inc_tun_rx_ignored_bytes(frame.size - sizeof(struct tun_frame_info));
	}
}

void IpLink::write_packet(std::uint8_t frame_type, const void *data, size_t size)
{
	auto oit = std::back_inserter(uart_tx_buf);
	oit = encoder.open(oit);
	/* Write packet type */
	oit = encoder.write(&frame_type, 1, oit);
	/* Write payload */
	oit = encoder.write(data, size, oit);
	/* Write checksum */
	const std::uint32_t cs = htonl(calc_checksum(data, size) ^ frame_type);
	oit = encoder.write(&cs, sizeof(cs), oit);
	oit = encoder.close(oit);
}

std::tuple<std::uint8_t, void *, size_t> IpLink::read_packet()
{
	/* Get packet from queue */
	buffer = std::move(uart_rx_buf.front());
	uart_rx_buf.pop_front();
	/* Validate packet */
	auto p = static_cast<std::uint8_t *>(buffer.data());
	auto size = buffer.size();
	if (size < 5) {
		std::cerr << "TOOSMALL: " << size << std::endl;
		verbose_hexdump("UART =!> TUN [invalid length]", buffer.data(), buffer.size());
		stats.inc_uart_rx_errors(1);
		return { 0, nullptr, 0 };
	}
	std::uint8_t frame_type = *p;
	/* Verify checksum */
	std::uint32_t cs_expect = ntohl(*static_cast<std::uint32_t *>(static_cast<void *>(&p[size - 4])));
	p++;
	size -= 5;
	std::uint32_t cs_actual = calc_checksum(p, size) ^ frame_type;
	if (cs_expect != cs_actual) {
		std::cerr << "CSFAIL: " << std::hex << cs_expect << " != " << cs_actual << std::dec << std::endl;
		verbose_hexdump("UART =!> TUN [checksum fail]", buffer.data(), buffer.size());
		stats.inc_uart_rx_errors(1);
		return { 0, nullptr, 0 };
	}
	return { frame_type, p, size };
}

void IpLink::on_tun_writable()
{
	std::uint8_t frame_type;
	void *data;
	std::size_t size;
	std::tie(frame_type, data, size) = read_packet();
	if (data == nullptr) {
		return;
	}
	if (frame_type == ft_keepalive) {
		on_received_keepalive();
	} else if (frame_type == ft_ip_packet) {
		if (size < 20 + sizeof(struct tun_frame_info)) {
			stats.inc_uart_rx_errors(1);
			std::cerr << "TOOSMALLIP: " << size << std::endl;
			verbose_hexdump("UART =!> TUN [invalid IP packet length]", data, size);
			return;
		}
		on_received_keepalive();
		Frame frame(data, size);
		tun.send(frame);
		stats.inc_tun_tx_frames(1);
		stats.inc_tun_tx_bytes(frame.size - sizeof(struct tun_frame_info));
		verbose_hexdump("UART ==> TUN", frame.buffer, frame.size);
	} else {
		stats.inc_uart_rx_errors(1);
		std::cerr << "INVALIDTYPE: " << frame_type << std::endl;
		verbose_hexdump("UART =!> TUN [invalid type]", data, size);
		return;
	}
}

static constexpr Linux::Flags flags = Linux::close_on_exec | Linux::non_blocking;

#define bind_handler(method) \
	[this] (auto events) { \
		method(events); \
	}

IpLink::IpLink(const Config& config) :
	config(config),
	sfd({ sig_int, sig_term, sig_quit, sig_usr1 }, true, flags),
	meter_timer(Linux::Clock::monotonic, flags),
	send_ka(Linux::Clock::monotonic, flags),
	recv_ka(Linux::Clock::monotonic, flags),
	uart(config.uart, config.baud, flags),
	tun(config.ifname, flags),
	epfd(Flags::close_on_exec),
	decoder(sizeof(struct tun_frame_info) + config.mtu)
{
	tun.set_point_to_point(true);
	tun.set_mtu(config.mtu);
	tun.set_addr(config.addr.get_address(), config.addr.get_mask());
	// tun.set_route(remote_addr, 1, remote_addr, link_mask);

	epfd.bind(sfd, bind_handler(on_signal), Events::event_in);
	epfd.bind(meter_timer, bind_handler(on_update_meter), Events::event_in);
	epfd.bind(send_ka, bind_handler(on_send_ka_timer), Events::event_in);
	epfd.bind(recv_ka, bind_handler(on_recv_ka_timer), Events::event_in);
	epfd.bind(uart, bind_handler(on_serial), Events::event_in);
	epfd.bind(tun, bind_handler(on_tun), Events::event_in);

	if (!config.updown) {
		set_tun_updown(true);
	}
}

void IpLink::run()
{
	if (config.meter) {
		rx_meter = { 15, 0.5 };
		tx_meter = { 15, 0.5 };
		Linux::TimerFD::TimeSpec now;
		now.tv_sec = 0;
		now.tv_nsec = 1;
		Linux::TimerFD::TimeSpec interval;
		interval.tv_sec = 0;
		interval.tv_nsec = 500000000;
		meter_timer.set_periodic(now, interval);
	}
	reset_send_ka_timer();
	reset_recv_ka_timer();
	send_keepalive();
	rebind_events();
	while (!terminating) {
		epfd.wait();
	}
	if (config.meter) {
		std::cerr << std::endl;
	}
}

}
