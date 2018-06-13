#pragma once
#include <deque>

template <typename Value, typename Interval>
class Meter
{
	std::size_t max_len{0};
	Interval interval{};
	std::deque<Value> history;

	void trim()
	{
		while (history.size() > max_len) {
			history.pop_back();
		}
	}

public:

	Meter() = default;

	Meter(std::size_t history_len, Interval interval) :
		max_len(history_len),
		interval(interval)
	{
	}

	void clear()
	{
		history.clear();
	}

	void write(Value value)
	{
		history.emplace_front(std::move(value));
		trim();
	}

	void set_history_length(std::size_t value)
	{
		max_len = value;
		trim();
	}

	void set_interval(Interval value)
	{
		interval = std::move(value);
	}

	std::size_t size() const
	{
		return history.size();
	}

	Value& get(std::size_t idx)
	{
		return history[idx];
	}

	auto diff(std::size_t idx)
	{
		return history[0] - history[idx];
	}

	auto rate(std::size_t idx)
	{
		return diff(idx) / (interval * idx);
	}

	auto diff()
	{
		return diff(history.size() - 1);
	}

	auto rate()
	{
		return rate(history.size() - 1);
	}

};
