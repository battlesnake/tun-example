#pragma once

#include <string>
#include <stdexcept>
#include <functional>
#include <utility>
#include <optional>
#include <map>

#include <cstdint>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/wait.h>

#if ! defined TFD_TIMER_CANCEL_ON_SET
#define TFD_TIMER_CANCEL_ON_SET (1 << 1)
#endif

namespace Linux {

using byte_type = std::uint8_t;

enum Flags
{
	none = 0,
	non_blocking = 1,
	close_on_exec = 2
};

inline constexpr Flags operator | (Flags a, Flags b)
{
	return static_cast<Flags>(static_cast<int>(a) | static_cast<int>(b));
}

/* Error handling */

struct SystemError :
	std::runtime_error
{
	int code;
	static std::string make_message(const std::string& message, int code)
	{
		if (message.empty()) {
			return "System call failed with code " + std::to_string(code) + " (" + strerror(code) + ")";
		} else {
			return message;
		}
	}
	SystemError(const std::string& message, int code) :
		runtime_error(make_message(message, code)),
		code(code)
	{
	}
	SystemError(const std::string& message) :
		SystemError(message, errno)
	{
	}
	SystemError(int code) :
		SystemError("", code)
	{
	}
	SystemError() :
		SystemError("", errno)
	{
	}
};

struct SysCallFailed :
	SystemError
{
	static std::string make_message(const std::string& call, int code)
	{
		return "System call \"" + call + "\" failed with code " + std::to_string(code) + " (" + strerror(code) + ")";
	}
	SysCallFailed(const std::string& call) :
		SystemError(make_message(call, errno))
	{
	}
};

struct InvalidParameter :
	std::runtime_error
{
	using std::runtime_error::runtime_error;
};

namespace detail {

static void assert_zero(const std::string& call, int ret)
{
	if (ret) {
		throw SysCallFailed(call);
	}
}

static size_t assert_not_negative(const std::string& call, ssize_t ret)
{
	if (ret < 0) {
		throw SysCallFailed(call);
	} else {
		return size_t(ret);
	}
}

static std::optional<size_t> try_result(ssize_t value)
{
	return value >= 0 ? std::make_optional(size_t(value)) : std::nullopt;
}

static int translate_flags(Flags flags, int non_blocking, int close_on_exec)
{
	int ret = 0;
	if (flags & Flags::non_blocking) {
		if (non_blocking) {
			ret |= non_blocking;
		} else {
			throw InvalidParameter("Unsupported flag: non-blocking");
		}
	}
	if (flags & Flags::close_on_exec) {
		if (close_on_exec) {
			ret |= non_blocking;
		} else {
			throw InvalidParameter("Unsupported flag: close_on_exec");
		}
	}
	return ret;
}

}

/* Interfaces */

struct Closeable
{
	virtual void close() = 0;
};

struct Readable
{
	virtual size_t read(void *buf, size_t size) = 0;
	virtual std::optional<size_t> try_read(void *buf, size_t size) = 0;
	void read(std::vector<byte_type>& buf)
	{
		buf.resize(read(buf.data(), buf.size()));
	}
	void try_read(std::vector<byte_type>& buf)
	{
		buf.resize(try_read(buf.data(), buf.size()).value_or(0));
	}
};

struct Writable
{
	virtual size_t write(const void *buf, size_t size) = 0;
	virtual std::optional<size_t> try_write(const void *buf, size_t size) = 0;
	void write(const std::vector<byte_type>& buf)
	{
		write(buf.data(), buf.size());
	}
	std::optional<size_t> try_write(const std::vector<byte_type>& buf)
	{
		return try_write(buf.data(), buf.size());
	}
};

enum SeekOrigin
{
	seek_start = SEEK_SET,
	seek_current = SEEK_CUR,
	seek_end = SEEK_END
};

struct Seekable
{
	virtual size_t seek(ssize_t displacement, SeekOrigin origin = seek_start) = 0;
	virtual std::optional<size_t> try_seek(ssize_t displacement, SeekOrigin origin = seek_start) = 0;
	virtual size_t tell() = 0;
	size_t seek(size_t displacement)
	{
		return seek(ssize_t(displacement), seek_start);
	}
	size_t skip(size_t displacement)
	{
		return seek(ssize_t(displacement), seek_current);
	}
};

struct FD
{
	virtual int get_fd() const = 0;
};

/* Syscall implementations */

struct FileDescriptor :
	virtual FD,
	Closeable
{
private:
	int fd;
public:
	virtual int get_fd() const override
	{
		return fd;
	}
	explicit FileDescriptor(int fd) :
		fd(fd)
	{
	}
	FileDescriptor() :
		FileDescriptor(-1)
	{
	}
	FileDescriptor(int fd, const std::string& call) :
		FileDescriptor(fd)
	{
		detail::assert_not_negative(call, fd);
		if (fd == -1) {
			throw SysCallFailed(call);
		}
	}
	FileDescriptor(const FileDescriptor& f) = delete;
	void operator = (const FileDescriptor& f) = delete;
	FileDescriptor(FileDescriptor&& f) :
		fd(-1)
	{
		std::swap(fd, f.fd);
	}
	void operator = (FileDescriptor&& f)
	{
		close();
		std::swap(fd, f.fd);
	}
	~FileDescriptor()
	{
		close();
	}

	int release()
	{
		int ret = -1;
		std::swap(ret, fd);
		return ret;
	}

	void close()
	{
		if (fd != -1) {
			if (::close(fd)) {
				throw SysCallFailed("close");
			}
			fd = -1;
		}
	}
	void set_flags(Flags flags)
	{
		set_nonblock(flags & Flags::non_blocking);
		set_cloexec(flags & Flags::close_on_exec);
	}

	template <typename... Args>
	int fcntl(int cmd, Args&&... args)
	{
		return detail::assert_not_negative("fcntl", ::fcntl(fd, cmd, std::forward<Args>(args)...));
	}

	template <typename... Args>
	int ioctl(unsigned long request, Args&&... args)
	{
		return detail::assert_not_negative("ioctl", ::ioctl(fd, request, std::forward<Args>(args)...));
	}

	/*
	 * Race condition with fork() when setting after creation, prefer
	 * fd-creation functions which allow you to set it from the start,
	 * e.g: using Flags::non_block.
	 */
	void set_cloexec(bool value)
	{
		auto flags = fcntl(F_GETFD);
		if (value) {
			flags |= FD_CLOEXEC;
		} else {
			flags &= ~FD_CLOEXEC;
		}
		fcntl(F_SETFD, flags);
	}
	void set_nonblock(bool value)
	{
		auto flags = fcntl(F_GETFL);
		if (value) {
			flags |= O_NONBLOCK;
		} else {
			flags &= ~O_NONBLOCK;
		}
		fcntl(F_SETFL, flags);
	}
	FileDescriptor dup(int target = -1)
	{
		if (target == -1) {
			return { ::dup(fd), "dup" };
		} else {
			return { ::dup2(fd, target), "dup2" };
		}
	}
};

struct ReadableFileDescriptor :
	virtual FD,
	Readable
{
	using Readable::read;
	size_t read(void *buf, size_t size) override
	{
		return detail::assert_not_negative("read", ::read(get_fd(), buf, size));
	}
	std::optional<size_t> try_read(void *buf, size_t size) override
	{
		return detail::try_result(::read(get_fd(), buf, size));
	}
};

struct WritableFileDescriptor :
	virtual FD,
	Writable
{
	using Writable::write;
	size_t write(const void *buf, size_t size) override
	{
		return detail::assert_not_negative("write", ::write(get_fd(), buf, size));
	}
	std::optional<size_t> try_write(const void *buf, size_t size) override
	{
		return detail::try_result(::write(get_fd(), buf, size));
	}
};

struct SeekableFileDescriptor :
	virtual FD,
	Seekable
{
	size_t seek(ssize_t displacement, SeekOrigin origin = seek_start) override
	{
		return detail::assert_not_negative("lseek", lseek(get_fd(), displacement, origin));
	}
	std::optional<size_t> try_seek(ssize_t displacement, SeekOrigin origin = seek_start) override
	{
		return detail::try_result(lseek(get_fd(), displacement, origin));
	}
	size_t tell() override
	{
		return seek(0, seek_current);
	}
};

/* Concrete implementations */

enum FileAccessMode
{
	file_read = O_RDONLY,
	file_write = O_WRONLY,
	file_read_write = O_RDWR
};

enum FileFlags
{
	file_none = 0,
	file_create = O_CREAT,
	file_append = O_APPEND,
	file_truncate = O_TRUNC,
	file_path = O_PATH
};

struct File :
	FileDescriptor
{
	using Stats = struct stat;
	File(const std::string& path, FileAccessMode access_mode, FileFlags file_flags, Flags flags = Flags::none, int mode = 000) :
		FileDescriptor(construct(path.c_str(), access_mode, file_flags, flags, mode), "open")
	{
	}
	File(const std::string& path, int flags, int mode) :
		FileDescriptor(open(path.c_str(), flags, mode), "open")
	{
	}
	Stats stat()
	{
		Stats s;
		detail::assert_zero("fstat", fstat(get_fd(), &s));
		return s;
	}
private:
	static int construct(const char *path, FileAccessMode access_mode, FileFlags file_flags, Flags flags, int mode)
	{
		int f = int(mode) | int(access_mode) | int(file_flags) | detail::translate_flags(flags, O_NONBLOCK, O_CLOEXEC);
		return open(path, f, mode);
	}
};

struct SeekableFile :
	FileDescriptor,
	SeekableFileDescriptor
{
	using Stats = struct stat;
	SeekableFile(const std::string& path, FileAccessMode access_mode, FileFlags file_flags, Flags flags = Flags::none, int mode = 000) :
		FileDescriptor(construct(path.c_str(), access_mode, file_flags, flags, mode), "open")
	{
	}
	SeekableFile(const std::string& path, int flags, int mode) :
		FileDescriptor(open(path.c_str(), flags, mode), "open")
	{
	}
	Stats stat()
	{
		Stats s;
		detail::assert_zero("fstat", fstat(get_fd(), &s));
		return s;
	}
	size_t size(bool use_seek = false)
	{
		return use_seek ? seek(0, seek_end) : stat().st_size;
	}
	void resize(size_t size)
	{
		detail::assert_zero("ftruncate", ftruncate(get_fd(), size));
	}
private:
	static int construct(const char *path, FileAccessMode access_mode, FileFlags file_flags, Flags flags, int mode)
	{
		int f = int(mode) | int(access_mode) | int(file_flags) | detail::translate_flags(flags, O_NONBLOCK, O_CLOEXEC);
		return open(path, f, mode);
	}
};

struct Pipe :
	Readable,
	Writable,
	Closeable
{
	struct Input :
		FileDescriptor,
		WritableFileDescriptor
	{
		using FileDescriptor::FileDescriptor;
		using FileDescriptor::operator =;
	};
	struct Output :
		FileDescriptor,
		ReadableFileDescriptor
	{
		using FileDescriptor::FileDescriptor;
		using FileDescriptor::operator =;
	};
	Output output;
	Input input;
	Pipe()
	{
		int p[2];
		detail::assert_zero("pipe2", ::pipe2(p, O_NONBLOCK));
		output = Output{ p[0], "pipe2" };
		input = Input{ p[1], "pipe2" };
	}
	size_t read(void *buf, size_t size)
	{
		return output.read(buf, size);
	}
	size_t write(const void *buf, size_t size)
	{
		return input.write(buf, size);
	}
	std::optional<size_t> try_read(void *buf, size_t size)
	{
		return output.try_read(buf, size);
	}
	std::optional<size_t> try_write(const void *buf, size_t size)
	{
		return input.try_write(buf, size);
	}
	void close()
	{
		input.close();
		output.close();
	}
};

struct EventFD :
	FileDescriptor,
	private Readable,
	private Writable
{
	EventFD(unsigned int initial_value, bool semaphore, Flags flags) :
		FileDescriptor(eventfd(initial_value, (semaphore ? EFD_SEMAPHORE : 0) | detail::translate_flags(flags, EFD_NONBLOCK, EFD_CLOEXEC)), "eventfd")
	{
	}
protected:
	uint64_t event_read()
	{
		uint64_t amount;
		read(&amount, sizeof(amount));
		return amount;
	}
	void event_write(uint64_t amount)
	{
		write(&amount, sizeof(amount));
	}
	std::optional<uint64_t> try_event_read()
	{
		uint64_t amount;
		auto ret = try_read(&amount, sizeof(amount));
		return ret.has_value() ? std::make_optional(amount) : std::nullopt;
	}
};

/* Classic semaphore */
struct Semaphore :
	EventFD
{
	Semaphore(unsigned int initial_value = 0, Flags flags = Flags::none) :
		EventFD(initial_value, true, flags)
	{
	}
	void take()
	{
		event_read();
	}
	void give(uint64_t amount = 1)
	{
		event_write(amount);
	}
	bool try_take()
	{
		return try_event_read().has_value();
	}
};

/* Counter, reads reset on success and block or fail if counter is zero */
class Counter :
	EventFD
{
	Counter(unsigned int initial_value = 0, Flags flags = Flags::none) :
		EventFD(initial_value, false, flags)
	{
	}
	uint64_t read_and_reset() {
		return event_read();
	}
	void increment(uint64_t amount = 1) {
		event_write(amount);
	}
	std::optional<uint64_t> try_read_and_reset() {
		return try_event_read();
	}
};

/*
 * kill -l | \
 *   perl -ne 'chomp; print "$_ ";' | \
 *   perl -pe 's/(?<=\w)\s+/\n/g' | \
 *   perl -pe 's/\+/_P_/; s/-/_M_/; s/(\d+)\)\s*(\S+)/\2 = \1,/' | \
 *   tr '[:upper:]' '[:lower:]'
 */
enum Signal
{
	sig_hup = 1,
	sig_int = 2,
	sig_quit = 3,
	sig_ill = 4,
	sig_trap = 5,
	sig_abrt = 6,
	sig_bus = 7,
	sig_fpe = 8,
	sig_kill = 9,
	sig_usr1 = 10,
	sig_segv = 11,
	sig_usr2 = 12,
	sig_pipe = 13,
	sig_alrm = 14,
	sig_term = 15,
	sig_stkflt = 16,
	sig_chld = 17,
	sig_cont = 18,
	sig_stop = 19,
	sig_tstp = 20,
	sig_ttin = 21,
	sig_ttou = 22,
	sig_urg = 23,
	sig_xcpu = 24,
	sig_xfsz = 25,
	sig_vtalrm = 26,
	sig_prof = 27,
	sig_winch = 28,
	sig_io = 29,
	sig_pwr = 30,
	sig_sys = 31,
	sig_rtmin = 34,
	sig_rtmin_p_1 = 35,
	sig_rtmin_p_2 = 36,
	sig_rtmin_p_3 = 37,
	sig_rtmin_p_4 = 38,
	sig_rtmin_p_5 = 39,
	sig_rtmin_p_6 = 40,
	sig_rtmin_p_7 = 41,
	sig_rtmin_p_8 = 42,
	sig_rtmin_p_9 = 43,
	sig_rtmin_p_10 = 44,
	sig_rtmin_p_11 = 45,
	sig_rtmin_p_12 = 46,
	sig_rtmin_p_13 = 47,
	sig_rtmin_p_14 = 48,
	sig_rtmin_p_15 = 49,
	sig_rtmax_m_14 = 50,
	sig_rtmax_m_13 = 51,
	sig_rtmax_m_12 = 52,
	sig_rtmax_m_11 = 53,
	sig_rtmax_m_10 = 54,
	sig_rtmax_m_9 = 55,
	sig_rtmax_m_8 = 56,
	sig_rtmax_m_7 = 57,
	sig_rtmax_m_6 = 58,
	sig_rtmax_m_5 = 59,
	sig_rtmax_m_4 = 60,
	sig_rtmax_m_3 = 61,
	sig_rtmax_m_2 = 62,
	sig_rtmax_m_1 = 63,
	sig_rtmax = 64,
};

struct SignalSet
{
	sigset_t value;
	static const SignalSet empty;
	static const SignalSet full;
	const sigset_t& get_fd() const
	{
		return value;
	}
	explicit SignalSet(const sigset_t& value) :
		value(value)
	{
	}
	SignalSet(const std::initializer_list<Signal>& signals) :
		SignalSet(false)
	{
		for (const auto signal : signals) {
			add(signal);
		}
	}
	SignalSet(const SignalSet&) = default;
	SignalSet& operator =(const SignalSet&) = default;
	explicit SignalSet(bool filled = false)
	{
		if (filled) {
			fill();
		} else {
			clear();
		}
	}
	void add(Signal signal)
	{
		detail::assert_zero("sigaddset", sigaddset(&value, signal));
	}
	void remove(Signal signal)
	{
		detail::assert_zero("sigdelset", sigdelset(&value, signal));
	}
	bool has(Signal signal)
	{
		return sigismember(&value, signal);
	}
	void clear()
	{
		detail::assert_zero("sigclearset", sigemptyset(&value));
	}
	void fill()
	{
		detail::assert_zero("sigfillset", sigfillset(&value));
	}
	SignalSet block() const
	{
		sigset_t prev;
		detail::assert_zero("sigprocmask", sigprocmask(SIG_BLOCK, &value, &prev));
		return SignalSet(prev);
	}
	SignalSet unblock() const
	{
		sigset_t prev;
		detail::assert_zero("sigprocmask", sigprocmask(SIG_UNBLOCK, &value, &prev));
		return SignalSet(prev);
	}
	SignalSet set_mask() const
	{
		sigset_t prev;
		detail::assert_zero("sigprocmask", sigprocmask(SIG_SETMASK, &value, &prev));
		return SignalSet(prev);
	}
};

struct CurrentThread
{
	SignalSet signal_block(const SignalSet& ss)
	{
		sigset_t prev;
		detail::assert_zero("sigprocmask", sigprocmask(SIG_BLOCK, &ss.value, &prev));
		return SignalSet(prev);
	}
	SignalSet signal_unblock(const SignalSet& ss)
	{
		sigset_t prev;
		detail::assert_zero("sigprocmask", sigprocmask(SIG_UNBLOCK, &ss.value, &prev));
		return SignalSet(prev);
	}
	SignalSet signal_setmask(const SignalSet& ss)
	{
		sigset_t prev;
		detail::assert_zero("sigprocmask", sigprocmask(SIG_SETMASK, &ss.value, &prev));
		return SignalSet(prev);
	}
};

extern CurrentThread current_thread;

struct SignalFD :
	FileDescriptor,
	private ReadableFileDescriptor
{
	SignalFD(const SignalSet& ss, bool block, Flags flags = Flags::none) :
		FileDescriptor(signalfd(-1, &ss.get_fd(), detail::translate_flags(flags, EFD_NONBLOCK, EFD_CLOEXEC)), "signalfd")
	{
		if (block) {
			current_thread.signal_block(ss);
		}
	}
	void update(const SignalSet& ss)
	{
		detail::assert_not_negative("signalfd", signalfd(get_fd(), &ss.get_fd(), 0));
	}
	signalfd_siginfo take_signal()
	{
		signalfd_siginfo ssi;
		read(&ssi, sizeof(ssi));
		return ssi;
	}
	std::optional<signalfd_siginfo> try_take_signal()
	{
		signalfd_siginfo ssi;
		return try_read(&ssi, sizeof(ssi)).has_value() ? std::make_optional(ssi) : std::nullopt;
	}
};

enum Clock
{
	real_time = CLOCK_REALTIME,
	monotonic = CLOCK_MONOTONIC,
	boot_time = CLOCK_BOOTTIME,
	boot_time_alarm = CLOCK_BOOTTIME_ALARM,
	real_time_alarm = CLOCK_REALTIME_ALARM
};

struct TimerFD :
	FileDescriptor,
	private ReadableFileDescriptor
{
	using TimeSpec = timespec;

	TimerFD(Clock clock, Flags flags = Flags::none) :
		FileDescriptor(timerfd_create(clock, detail::translate_flags(flags, TFD_NONBLOCK, TFD_CLOEXEC)), "timerfd_create")
	{
	}

	void set_absolute(const TimeSpec& deadline, bool cancel_on_set)
	{
		itimerspec ts;
		ts.it_value = deadline;
		ts.it_interval = { 0, 0 };
		detail::assert_zero("timerfd_settime", timerfd_settime(get_fd(), TFD_TIMER_ABSTIME | (cancel_on_set ? TFD_TIMER_CANCEL_ON_SET : 0), &ts, NULL));
	}

	void set_periodic(const TimeSpec& base, const TimeSpec& interval)
	{
		itimerspec ts;
		ts.it_value = base;
		ts.it_interval = interval;
		detail::assert_zero("timerfd_settime", timerfd_settime(get_fd(), 0, &ts, NULL));
	}

	std::uint64_t read_tick_count()
	{
		std::uint64_t count;
		read(&count, sizeof(count));
		return count;
	}

	std::uint64_t try_read_tick_count()
	{
		std::uint64_t count;
		return try_read(&count, sizeof(count)).has_value() ? count : 0;
	}
};

struct EpollFD :
	FileDescriptor
{
	enum Events
	{
		event_none = 0,
		/* Input / output */
		event_in = EPOLLIN,
		event_out = EPOLLOUT,
		/* Socket peer closed write-side of connection */
		event_rd_hup = EPOLLRDHUP,
		/* Exceptional condition */
		event_pri = EPOLLPRI,
		/* Hang-up (peer closed connection, may still be readable) */
		event_hup = EPOLLHUP
	};
	using Handler = std::function<void(Events)>;
private:
	std::map<int, Handler> handlers;
public:
	enum Trigger
	{
		/* Level-triggered */
		trigger_level = 0,
		/* Edge-triggered */
		trigger_edge = EPOLLET,
		/*
		 * After event is received, this descriptor has its event-mask
		 * zeroed until re-enabled with EPOLL_CTL_MOD
		 */
		trigger_oneshot = EPOLLONESHOT
	};
	enum PowerOptions
	{
		power_opt_none = 0,
		/* Do not sleep system while event is beind handled */
		power_opt_wake_up = EPOLLWAKEUP,
		/* See epoll_ctl(2) */
		power_opt_exclusive = EPOLLEXCLUSIVE
	};
	EpollFD(Flags flags = Flags::none) :
		FileDescriptor(epoll_create1(detail::translate_flags(flags, 0, EPOLL_CLOEXEC)), "epoll_create1")
	{
	}
	void bind(const FileDescriptor& fd, Handler handler, Events events, Trigger trigger = trigger_level, PowerOptions power_options = power_opt_none)
	{
		auto it = handlers.insert_or_assign(fd.get_fd(), std::move(handler));
		epoll_event ee;
		ee.events = int(events) | int(trigger) | int(power_options);
		ee.data.fd = fd.get_fd();
		try {
			detail::assert_zero("epoll_ctl", epoll_ctl(get_fd(), EPOLL_CTL_ADD, fd.get_fd(), &ee));
		} catch (SystemError& e) {
			handlers.erase(it.first);
			throw;
		}
	}
	void rebind(const FileDescriptor& fd, Events events, Trigger trigger = trigger_level, PowerOptions power_options = power_opt_none)
	{
		epoll_event ee;
		ee.events = int(events) | int(trigger) | int(power_options);
		ee.data.fd = fd.get_fd();
		detail::assert_zero("epoll_ctl", epoll_ctl(get_fd(), EPOLL_CTL_MOD, fd.get_fd(), &ee));
	}
	void unbind(const FileDescriptor& fd)
	{
		detail::assert_zero("epoll_ctl", epoll_ctl(get_fd(), EPOLL_CTL_DEL, fd.get_fd(), NULL));
		handlers.erase(fd.get_fd());
	}
	int wait(int max_events = 1, int timeout = -1, const std::optional<SignalSet>& signal_mask = std::nullopt)
	{
		std::vector<epoll_event> events;
		events.resize(max_events);
		auto count = detail::assert_not_negative("epoll_wait", epoll_pwait(get_fd(), events.data(), events.size(), timeout, signal_mask.has_value() ? &signal_mask->get_fd() : nullptr));
		events.resize(count);
		for (const auto& event : events) {
			auto& handler = handlers.at(event.data.fd);
			handler(Events(event.events));
		}
		return count;
	}
};

inline constexpr EpollFD::Events operator | (EpollFD::Events a, EpollFD::Events b)
{
	return static_cast<EpollFD::Events>(static_cast<int>(a) | static_cast<int>(b));
}

struct Socket :
	FileDescriptor
{
	enum Domain
	{
		domain_unix = AF_UNIX,
		domain_local = AF_LOCAL,
		domain_ipv4 = AF_INET,
		domain_ipv6 = AF_INET6,
		domain_ipx = AF_IPX,
		domain_netlink = AF_NETLINK,
		domain_x25 = AF_X25,
		domain_ax25 = AF_AX25,
		domain_atm_pvc = AF_ATMPVC,
		domain_appletalk = AF_APPLETALK,
		domain_raw_packet = AF_PACKET,
		domain_crypto = AF_ALG
	};
	enum Type
	{
		type_stream = SOCK_STREAM,
		type_datagram = SOCK_DGRAM,
		type_seq_packet = SOCK_SEQPACKET,
		type_raw = SOCK_RAW,
		type_rdm = SOCK_RDM
	};
	enum RecvFlags
	{
		recv_default = 0,
		recv_cmsg_close_on_exec = MSG_CMSG_CLOEXEC,
		recv_dont_wait = MSG_DONTWAIT,
		recv_error_from_queue = MSG_ERRQUEUE,
		recv_oob = MSG_OOB,
		recv_peek = MSG_PEEK,
		recv_truncate = MSG_TRUNC,
		recv_wait_all = MSG_WAITALL
	};
	enum SendFlags
	{
		send_default = 0,
		send_confirm = MSG_CONFIRM,
		send_direct = MSG_DONTROUTE,
		send_dont_wait = MSG_DONTWAIT,
		send_delimit = MSG_EOR,
		send_have_more = MSG_MORE,
		send_no_sigpipe = MSG_NOSIGNAL,
		send_oob = MSG_OOB
	};
	struct Address
	{
		socklen_t length;
		struct sockaddr address;
	};
	Socket(Domain domain, Type type, Flags flags = Flags::none, int protocol = 0) :
		FileDescriptor(socket(int(domain), int(type) | detail::translate_flags(flags, SOCK_NONBLOCK, SOCK_CLOEXEC), protocol), "socket")
	{
	}
protected:
	explicit Socket(int fd) :
		FileDescriptor(fd)
	{
	}
};

struct ReadableSocket :
	virtual Socket,
	ReadableFileDescriptor
{
	using RecvFlags = Socket::RecvFlags;
	using Address = Socket::Address;
	size_t recv(void *buf, size_t size, RecvFlags flags = RecvFlags::recv_default)
	{
		return detail::assert_not_negative("recv", ::recv(get_fd(), buf, size, int(flags)));
	}
	size_t recv(void *buf, size_t size, Address& sender, RecvFlags flags = RecvFlags::recv_default)
	{
		return detail::assert_not_negative("recvfrom", ::recvfrom(get_fd(), buf, size, int(flags), &sender.address, &sender.length));
	}
	std::optional<size_t> try_recv(void *buf, size_t size, RecvFlags flags = RecvFlags::recv_default)
	{
		return detail::assert_not_negative("recv", ::recv(get_fd(), buf, size, int(flags)));
	}
	std::optional<size_t> try_recv(void *buf, size_t size, Address& sender, RecvFlags flags = RecvFlags::recv_default)
	{
		return detail::assert_not_negative("recvfrom", ::recvfrom(get_fd(), buf, size, int(flags), &sender.address, &sender.length));
	}
protected:
	ReadableSocket() :
		Socket(-1)
	{
	}
};

struct WritableSocket :
	virtual Socket,
	WritableFileDescriptor
{
	using SendFlags = Socket::SendFlags;
	using Address = Socket::Address;
	size_t send(void *buf, size_t size, SendFlags flags = SendFlags::send_default)
	{
		return detail::assert_not_negative("send", ::send(get_fd(), buf, size, int(flags)));
	}
	size_t send(void *buf, size_t size, const Address& sender, SendFlags flags = SendFlags::send_default)
	{
		return detail::assert_not_negative("sendto", ::sendto(get_fd(), buf, size, int(flags), &sender.address, sender.length));
	}
	std::optional<size_t> try_send(void *buf, size_t size, SendFlags flags = SendFlags::send_default)
	{
		return detail::assert_not_negative("send", ::send(get_fd(), buf, size, int(flags)));
	}
	std::optional<size_t> try_send(void *buf, size_t size, const Address& sender, SendFlags flags = SendFlags::send_default)
	{
		return detail::assert_not_negative("sendto", ::sendto(get_fd(), buf, size, int(flags), &sender.address, sender.length));
	}
protected:
	WritableSocket() :
		Socket(-1)
	{
	}
};

struct SocketConnection :
	virtual Socket,
	ReadableSocket,
	WritableSocket
{
	using Address = Socket::Address;
	SocketConnection(Socket&& socket, const Address& address) :
		Socket(std::move(socket)),
		address(address)
	{
		detail::assert_zero("connect", connect(get_fd(), &address.address, address.length));
	}
	const Address& get_address() const
	{
		return address;
	}
private:
	friend struct ServerSocket;
	SocketConnection(int fd, const Address& address) :
		Socket(fd),
		address(address)
	{
	}
	Address address;
};

struct ServerSocket :
	Socket
{
	using Address = Socket::Address;
	ServerSocket(Socket&& socket, const Address& address, int backlog) :
		Socket(std::move(socket))
	{
		detail::assert_zero("bind", bind(get_fd(), &address.address, address.length));
		detail::assert_zero("listen", listen(get_fd(), backlog));
	}
	SocketConnection accept(Flags flags = Flags::none)
	{
		Address address;
		int fd = detail::assert_not_negative("accept", ::accept4(get_fd(), &address.address, &address.length, detail::translate_flags(flags, SOCK_NONBLOCK, SOCK_CLOEXEC)));
		return SocketConnection(fd, address);
	}
};

struct ChildProcess
{
private:
	pid_t pid;
public:
	explicit ChildProcess(const std::function<int()>& child_entry) :
		pid(detail::assert_not_negative("fork", fork()))
	{
		if (pid == 0) {
			int ret = child_entry();
			exit(ret);
		}
	}
	explicit ChildProcess(pid_t pid) :
		pid(pid)
	{
	}
	ChildProcess(const ChildProcess& f) = delete;
	void operator = (const ChildProcess& f) = delete;
	ChildProcess(ChildProcess&& f) :
		pid(-1)
	{
		std::swap(pid, f.pid);
	}
	void operator = (ChildProcess&& f)
	{
		sigkill();
		std::swap(pid, f.pid);
	}
	operator pid_t () const
	{
		return pid;
	}
	~ChildProcess()
	{
		sigkill();
	}
	void kill(int signo)
	{
		detail::assert_zero("kill", ::kill(pid, signo));
	}
	int wait(int flags = 0)
	{
		int wstatus;
		detail::assert_not_negative("waitpid", ::waitpid(pid, &wstatus, flags));
		if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
			pid = -1;
		}
		return wstatus;
	}
private:
	void sigkill()
	{
		if (pid > 0) {
			kill(SIGKILL);
			wait();
			pid = -1;
		}
	}
};

}
