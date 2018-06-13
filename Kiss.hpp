#pragma once

/*
 * Implementation of KISS/SLIP coding scheme, for serialising/deserialising
 * packets over a serial character link.
 */

#include <cstddef>
#include <cstdint>
#include <vector>
#include <list>
#include <optional>

namespace Kiss {

using Buffer = std::vector<std::uint8_t>;

struct Config
{
	static constexpr std::uint8_t FEND = 0xc0;
	static constexpr std::uint8_t FESC = 0xdb;
	static constexpr std::uint8_t TFEND = 0xdb;
	static constexpr std::uint8_t TFESC = 0xdd;
};

class Encoder
{
public:
	template <typename OutputIt>
	OutputIt open(OutputIt oit)
	{
		*oit++ = Config::FEND;
		return oit;
	}

	template <typename OutputIt>
	OutputIt close(OutputIt oit)
	{
		*oit++ = Config::FEND;
		return oit;
	}

	template <typename InputIt, typename OutputIt>
	OutputIt write(InputIt begin, InputIt end, OutputIt oit)
	{
		for (InputIt& it = begin; it != end; ++it) {
			const std::uint8_t byte = *it;
			if (byte == Config::FEND) {
				*oit++ = Config::FESC;
				*oit++ = Config::TFEND;
			} else if (byte == Config::FESC) {
				*oit++ = Config::FESC;
				*oit++ = Config::TFESC;
			} else {
				*oit++ = byte;
			}
		}
		return oit;
	}

	template <typename OutputIt>
	OutputIt write(const void *buf, std::size_t len, OutputIt oit)
	{
		const char *p = static_cast<const char *>(buf);
		return write(p, p + len, oit);
	}
};

class Decoder
{
	std::size_t max_packet_length;

	std::vector<std::uint8_t> packet;

	enum State {
		idle,
		error,
		active,
		active_escape
	};
	State state = idle;

	template <typename InputIt>
	std::list<std::vector<std::uint8_t>> decode_internal(InputIt begin, InputIt end)
	{
		std::list<std::vector<std::uint8_t>> packets;
		for (InputIt& it = begin; it != end; ++it) {
			const std::uint8_t in = *it;
			std::uint8_t out;
			/* Can we go from error state to idle */
			if (state == error) {
				if (in == Config::FEND) {
					state = idle;
				}
			}
			/* Can we go from idle state to active? */
			if (state == idle) {
				if (in != Config::FEND) {
					state = active;
					packet = {};
				}
			}
			/* Process packet contents/terminator */
			if (state == active) {
				if (in == Config::FESC) {
					/* Escape sequence */
					state = active_escape;
				} else if (in == Config::FEND) {
					/* End of packet */
					state = idle;
					packets.emplace_back(std::move(packet));
				} else {
					/* Verbatim */
					out = in;
				}
			} else if (state == active_escape) {
				/* Handle escape sequences (emit byte, return to normal mode) */
				if (in == Config::TFEND) {
					state = active;
					out = Config::FEND;
				} else if (in == Config::TFESC) {
					state = active;
					out = Config::FESC;
				} else {
					/* Invalid escape sequence: transition to error state */
					state = error;
				}
			}
			/* If we're in active state, emit a byte (unless buffer overflows) */
			if (state == active) {
				if (packet.size() == max_packet_length) {
					state = error;
				} else {
					packet.push_back(out);
				}
			}
		}
		return packets;
	}

public:
	Decoder(std::size_t max_packet_length) :
		max_packet_length(max_packet_length),
		packet(max_packet_length)
	{
	}

	template <typename InputIt>
	std::list<std::vector<std::uint8_t>> decode(InputIt begin, InputIt end)
	{
		return decode_internal(begin, end);
	}

	template <typename Container>
	std::list<std::vector<std::uint8_t>> decode(const Container& container)
	{
		return decode(container.cbegin(), container.cend());
	}
};

}
