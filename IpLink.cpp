extern "C" {
#include "hexdump.h"
}

#include "IpLink.hpp"

namespace IpLink {

using namespace Linux;

void IpLink::verbose_hexdump(const char *title, const void *buf, size_t len)
{
	if (config.verbose) {
		hexdump(title, buf, len);
	}
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
	}
	is_connected = value;
	if (config.updown) {
		set_tun_updown(value);
	}
}

void IpLink::update_timer(Linux::TimerFD& timer, unsigned delay, unsigned period)
{
	const auto billion = 1000000000L;
	const auto million = 1000000L;
	const auto thousand = 1000L;

	Linux::TimerFD::TimeSpec base;
	clock_gettime(Linux::Clock::monotonic, &base);
	base.tv_sec += delay / thousand;
	base.tv_nsec += delay % thousand * million;
	if (base.tv_nsec >= billion) {
		base.tv_nsec -= billion;
		base.tv_sec++;
	}

	Linux::TimerFD::TimeSpec interval;
	interval.tv_sec = period / thousand;
	interval.tv_nsec = period % thousand * million;

	timer.set_periodic(base, interval);
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

void IpLink::send_keepalive()
{
	char *x = nullptr;
	const auto buf = encoder.encode_packet(x, x);
	std::copy(buf.cbegin(), buf.cend(), std::back_inserter(uart_tx_buf));
	on_sent_keepalive();
}

void IpLink::on_sent_keepalive()
{
	update_timer(send_ka, config.keepalive_interval, config.keepalive_interval);
	verbose_hexdump("[keepalive]", NULL, 0);
}

void IpLink::on_received_keepalive()
{
	peer_state_changed(true);
	missed_keepalives = 0;
	update_timer(recv_ka, config.keepalive_interval, config.keepalive_interval);
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
	rebind_tun_events();
	rebind_serial_events();
}

void IpLink::on_tun(Events events)
{
	if (events & Events::event_in) {
		on_tun_readable();
	}
	if (events & Events::event_out) {
		on_tun_writable();
	}
	rebind_tun_events();
	rebind_serial_events();
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
	stats.inc_tun_rx_frames(1);
	const auto begin = reinterpret_cast<const std::uint8_t *>(frame.buffer);
	const auto end = begin + frame.size;
	const auto buf = encoder.encode_packet(begin, end);
	std::copy(buf.cbegin(), buf.cend(), std::back_inserter(uart_tx_buf));
	verbose_hexdump("TUN => UART", frame.buffer, frame.size);
}

void IpLink::on_tun_writable()
{
	const auto buf = std::move(uart_rx_buf.front());
	uart_rx_buf.pop_front();
	/* Handle message */
	if (buf.empty()) {
		/* Keep-alive, ignore */
	} else {
		Frame frame(static_cast<void *>(const_cast<std::uint8_t *>(buf.data())), buf.size());
		tun.send(frame);
		stats.inc_tun_tx_frames(1);
		verbose_hexdump("UART <= TUN", frame.buffer, frame.size);
	}
}

static constexpr Linux::Flags flags = Linux::close_on_exec | Linux::non_blocking;

IpLink::IpLink(const Config& config) :
	config(config),
	sfd({ sig_int, sig_term, sig_quit, sig_usr1 }, true, flags),
	send_ka(Linux::Clock::monotonic, flags),
	recv_ka(Linux::Clock::monotonic, flags),
	uart(config.uart, config.baud, flags),
	tun(config.ifname, flags),
	decoder(sizeof(struct tun_frame_info) + config.mtu)
{
	using namespace std::placeholders;

	tun.set_point_to_point(true);
	tun.set_mtu(config.mtu);
	tun.set_addr(config.addr.get_address(), config.addr.get_mask());
	// tun.set_route(remote_addr, 1, remote_addr, link_mask);

	if (!config.updown) {
		set_tun_updown(true);
	}

	epfd.bind(sfd, std::bind(&IpLink::on_signal, this, _1), Events::event_in);
	epfd.bind(send_ka, std::bind(&IpLink::on_send_ka_timer, this, _1), Events::event_in);
	epfd.bind(recv_ka, std::bind(&IpLink::on_recv_ka_timer, this, _1), Events::event_in);
	epfd.bind(uart, std::bind(&IpLink::on_serial, this, _1), Events::event_in);
	epfd.bind(tun, std::bind(&IpLink::on_tun, this, _1), Events::event_in);
}

void IpLink::run()
{
	update_timer(send_ka, 0, config.keepalive_interval);
	update_timer(recv_ka, 0, config.keepalive_interval);
	while (!terminating) {
		epfd.wait();
	}
}

}
