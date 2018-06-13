#pragma once

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

namespace IpLink {

class IpLink
{
	using Events = Linux::EpollFD::Events;
	using Frame = Linux::Tun::Frame;

	const Config& config;

	Linux::SignalFD sfd;
	Linux::TimerFD send_ka;
	Linux::TimerFD recv_ka;
	Linux::Serial uart;
	Linux::Tun tun;
	Linux::EpollFD epfd;

	Stats stats{};

	bool terminating{false};
	bool is_connected{false};
	bool tun_up{false};

	int missed_keepalives{1};

	std::list<std::vector<std::uint8_t>> uart_rx_buf;
	std::deque<std::uint8_t> uart_tx_buf;

	std::vector<std::uint8_t> buffer;

	Kiss::Encoder encoder;
	Kiss::Decoder decoder;

	void verbose_hexdump(const char *title, const void *buf, size_t len);

	void set_tun_updown(bool value);
	void peer_state_changed(bool value);

	void update_timer(Linux::TimerFD& timer, unsigned delay, unsigned period);

	void rebind_serial_events();
	void rebind_tun_events();

	void on_signal(Events events);
	void on_send_ka_timer(Events events);
	void on_recv_ka_timer(Events events);
	void on_serial(Events events);
	void on_tun(Events events);

	void on_serial_readable();
	void on_serial_writable();
	void on_tun_readable();
	void on_tun_writable();

	void send_keepalive();
	void on_sent_keepalive();
	void on_received_keepalive();
	void on_missed_keepalive();

public:
	IpLink(const Config& config);
	void run();
};

}