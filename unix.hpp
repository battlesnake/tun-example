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
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/wait.h>

namespace Unix {

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
		SystemError(message, code)
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

/* Interfaces */

enum Flags
{
	none = 0,
	non_blocking = 1,
	close_on_exec = 2
};

struct Closeable
{
	virtual void close() = 0;
};

struct Readable
{
	virtual size_t read(void *buf, size_t size) = 0;
	virtual std::optional<size_t> try_read(void *buf, size_t size) = 0;
};

struct Writable
{
	virtual size_t write(const void *buf, size_t size) = 0;
	virtual std::optional<size_t> try_write(const void *buf, size_t size) = 0;
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
};

struct FD
{
	virtual int get() const = 0;
};

/* Syscall implementations */

struct FileDescriptor :
	virtual FD,
	Closeable
{
private:
	int fd;
public:
	virtual int get() const override
	{
		return fd;
	}
	FileDescriptor(int fd) :
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
		assert_not_negative(call, fd);
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
	/*
	 * Race condition when setting, prefer fd-creation functions which allow
	 * you to set it from the start.
	 */
	void set_cloexec(bool value)
	{
		auto flags = fcntl(fd, F_GETFD);
		if (value) {
			flags |= FD_CLOEXEC;
		} else {
			flags &= ~FD_CLOEXEC;
		}
		assert_not_negative("fcntl (F_SETFD)", fcntl(fd, F_SETFD, flags));
	}
	void set_nonblock(bool value)
	{
		auto flags = fcntl(fd, F_GETFL);
		if (value) {
			flags |= O_NONBLOCK;
		} else {
			flags &= ~O_NONBLOCK;
		}
		assert_not_negative("fcntl (F_SETFL)", fcntl(fd, F_SETFL, flags));
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
	size_t read(void *buf, size_t size) override
	{
		return assert_not_negative("read", ::read(get(), buf, size));
	}
	std::optional<size_t> try_read(void *buf, size_t size) override
	{
		auto res = ::read(get(), buf, size);
		if (res < 0) {
			return std::nullopt;
		}
		return size_t(res);
	}
};

struct WritableFileDescriptor :
	virtual FD,
	Writable
{
	size_t write(const void *buf, size_t size) override
	{
		return assert_not_negative("write", ::write(get(), buf, size));
	}
	std::optional<size_t> try_write(const void *buf, size_t size) override
	{
		auto res = ::write(get(), buf, size);
		if (res < 0) {
			return std::nullopt;
		}
		return size_t(res);
	}
};

struct SeekableFileDescriptor :
	virtual FD,
	Seekable
{
	size_t seek(ssize_t displacement, SeekOrigin origin = seek_start) override
	{
		return assert_not_negative("lseek", lseek(get(), displacement, origin));
	}
	std::optional<size_t> try_seek(ssize_t displacement, SeekOrigin origin = seek_start) override
	{
		auto res = lseek(get(), displacement, origin);
		if (res < 0) {
			return std::nullopt;
		} else {
			return res;
		}
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
	file_create = O_CREAT,
	file_append = O_APPEND,
	file_truncate = O_TRUNC,
	file_path = O_PATH
};

struct File :
	FileDescriptor,
	ReadableFileDescriptor,
	WritableFileDescriptor,
	SeekableFileDescriptor
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
		assert_zero("fstat", fstat(get(), &s));
		return s;
	}
	size_t size(bool use_seek = false)
	{
		return use_seek ? seek(0, seek_end) : stat().st_size;
	}
	void resize(size_t size)
	{
		assert_zero("ftruncate", ftruncate(get(), size));
	}
private:
	static int construct(const char *path, FileAccessMode access_mode, FileFlags file_flags, Flags flags, int mode)
	{
		int f = int(mode) | int(access_mode) | int(file_flags) | (flags & Flags::non_blocking ? O_NONBLOCK : 0) | (flags & Flags::close_on_exec ? O_CLOEXEC : 0);
		return open(path, f, mode);
	}
};

template <typename Block>
struct BlockFile :
	File
{
	using block_type = Block;
	static constexpr auto block_size = sizeof(Block);
	void read_blocks(std::vector<Block>& buf)
	{
		read(buf.data(), buf.size() * block_size);
	}
	void write_blocks(const std::vector<Block&> buf)
	{
		write(buf.data(), buf.size() * block_size);
	}
	void read_blocks_at(ssize_t displacement, SeekOrigin origin, std::vector<Block>& buf)
	{
		seek(displacement, origin);
		read(buf.data(), buf.size() * block_size);
	}
	void write_blocks_at(ssize_t displacement, SeekOrigin origin, const std::vector<Block>& buf)
	{
		seek(displacement, origin);
		write(buf.data(), buf.size() * block_size);
	}
	size_t length_blocks()
	{
		return size() / block_size;
	}
	void resize_blocks(size_t count)
	{
		resize(count * block_size);
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
		assert_zero("pipe2", ::pipe2(p, O_NONBLOCK));
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
		FileDescriptor(eventfd(initial_value, (semaphore ? EFD_SEMAPHORE : 0) | (flags & Flags::non_blocking ? EFD_NONBLOCK : 0) | (flags & Flags::close_on_exec ? EFD_CLOEXEC : 0)), "eventfd")
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
		if (ret.has_value()) {
			return amount;
		} else {
			return std::nullopt;
		}
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
	const sigset_t& get() const
	{
		return value;
	}
	SignalSet(const sigset_t& value) :
		value(value)
	{
	}
	SignalSet(const SignalSet&) = default;
	SignalSet& operator =(const SignalSet&) = default;
	SignalSet(bool filled = false)
	{
		if (filled) {
			fill();
		} else {
			clear();
		}
	}
	void add(Signal signal)
	{
		assert_zero("sigaddset", sigaddset(&value, signal));
	}
	void remove(Signal signal)
	{
		assert_zero("sigdelset", sigdelset(&value, signal));
	}
	bool has(Signal signal)
	{
		return sigismember(&value, signal);
	}
	void clear()
	{
		assert_zero("sigclearset", sigemptyset(&value));
	}
	void fill()
	{
		assert_zero("sigfillset", sigfillset(&value));
	}
	SignalSet block() const
	{
		sigset_t prev;
		assert_zero("sigprocmask", sigprocmask(SIG_BLOCK, &value, &prev));
		return prev;
	}
	SignalSet unblock() const
	{
		sigset_t prev;
		assert_zero("sigprocmask", sigprocmask(SIG_UNBLOCK, &value, &prev));
		return prev;
	}
	SignalSet set_mask() const
	{
		sigset_t prev;
		assert_zero("sigprocmask", sigprocmask(SIG_SETMASK, &value, &prev));
		return prev;
	}
};
/* TODO: Move to cpp file */
const SignalSet SignalSet::empty{false};
const SignalSet SignalSet::full{true};

struct CurrentThread
{
	SignalSet signal_block(const SignalSet& ss)
	{
		sigset_t prev;
		assert_zero("sigprocmask", sigprocmask(SIG_BLOCK, &ss.value, &prev));
		return prev;
	}
	SignalSet signal_unblock(const SignalSet& ss)
	{
		sigset_t prev;
		assert_zero("sigprocmask", sigprocmask(SIG_UNBLOCK, &ss.value, &prev));
		return prev;
	}
	SignalSet signal_setmask(const SignalSet& ss)
	{
		sigset_t prev;
		assert_zero("sigprocmask", sigprocmask(SIG_SETMASK, &ss.value, &prev));
		return prev;
	}
};

/* TODO define in cpp file */
extern CurrentThread current_thread;

struct SignalFD :
	FileDescriptor,
	private Readable
{
	SignalFD(const SignalSet& ss, bool block, Flags flags = Flags::none) :
		FileDescriptor(signalfd(-1, &ss.get(), (flags & Flags::non_blocking ? EFD_NONBLOCK : 0) | (flags & Flags::close_on_exec ? EFD_CLOEXEC : 0)), "signalfd")
	{
		if (block) {
			current_thread.signal_block(ss);
		}
	}
	void update(const SignalSet& ss)
	{
		assert_not_negative("signalfd", signalfd(get(), &ss.get(), 0));
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
		if (try_read(&ssi, sizeof(ssi)).has_value()) {
			return ssi;
		} else {
			return std::nullopt;
		}
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
	private Readable
{
	using TimeSpec = timespec;

	TimerFD(Clock clock, Flags flags = Flags::none) :
		FileDescriptor(timerfd_create(clock, (flags & Flags::non_blocking ? TFD_NONBLOCK : 0) | (flags & Flags::close_on_exec ? TFD_CLOEXEC : 0)), "timerfd_create")
	{
	}

	void set_absolute(const TimeSpec& deadline, bool cancel_on_set)
	{
		itimerspec ts;
		ts.it_value = deadline;
		ts.it_interval = { 0, 0 };
		assert_zero("timerfd_settime", timerfd_settime(get(), TFD_TIMER_ABSTIME | (cancel_on_set ? TFD_TIMER_CANCEL_ON_SET : 0), &ts, NULL));
	}

	void set_periodic(const TimeSpec& base, const TimeSpec& interval)
	{
		itimerspec ts;
		ts.it_value = base;
		ts.it_interval = interval;
		assert_zero("timerfd_settime", timerfd_settime(get(), 0, &ts, NULL));
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
		FileDescriptor(epoll_create1((flags & close_on_exec ? EPOLL_CLOEXEC : 0)), "epoll_create1")
	{
	}
	void bind(const FileDescriptor& fd, Handler handler, Events events, Trigger trigger = trigger_level, PowerOptions power_options = power_opt_none)
	{
		auto it = handlers.insert_or_assign(fd.get(), std::move(handler));
		epoll_event ee;
		ee.events = int(events) | int(trigger) | int(power_options);
		ee.data.fd = fd.get();
		try {
			assert_zero("epoll_ctl", epoll_ctl(get(), EPOLL_CTL_ADD, fd.get(), &ee));
		} catch (SystemError& e) {
			handlers.erase(it.first);
			throw;
		}
	}
	void rebind(const FileDescriptor& fd, Events events, Trigger trigger = trigger_level, PowerOptions power_options = power_opt_none)
	{
		epoll_event ee;
		ee.events = int(events) | int(trigger) | int(power_options);
		ee.data.fd = fd.get();
		assert_zero("epoll_ctl", epoll_ctl(get(), EPOLL_CTL_MOD, fd.get(), &ee));
	}
	void unbind(const FileDescriptor& fd)
	{
		assert_zero("epoll_ctl", epoll_ctl(get(), EPOLL_CTL_DEL, fd.get(), NULL));
		handlers.erase(fd.get());
	}
	int wait(int max_events = 1, int timeout = -1, const std::optional<SignalSet>& signal_mask = std::nullopt)
	{
		std::vector<epoll_event> events;
		events.resize(max_events);
		auto count = assert_not_negative("epoll_wait", epoll_pwait(get(), events.data(), events.size(), timeout, signal_mask.has_value() ? &signal_mask->get() : nullptr));
		events.resize(count);
		for (const auto& event : events) {
			auto& handler = handlers.at(event.data.fd);
			handler(Events(event.events));
		}
		return count;
	}
};

class ChildProcess
{
protected:
	pid_t pid;
public:
	ChildProcess(const std::function<int()>& child_entry) :
		pid(assert_not_negative("fork", fork()))
	{
		if (pid == 0) {
			int ret = child_entry();
			exit(ret);
		}
	}
	ChildProcess(pid_t pid) :
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
		assert_zero("kill", ::kill(pid, signo));
	}
	int wait(int flags = 0)
	{
		int wstatus;
		assert_not_negative("waitpid", ::waitpid(pid, &wstatus, flags));
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
