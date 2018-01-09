#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#include <sys/signalfd.h>

#include "log.h"
#include "tun.h"
#include "reactor.h"

/* Main event-loop */
int event_loop(int tun, size_t mtu)
{
	int ret = 0;

	sigset_t ss;
	sigset_t oss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGTERM);
	sigaddset(&ss, SIGINT);
	if (sigprocmask(SIG_BLOCK, &ss, &oss) < 0) {
		perror("sigprocmask");
		return -1;
	}

	int sfd = signalfd(-1, &ss, SFD_NONBLOCK);
	if (sfd < 0) {
		perror("signalfd");
		goto fail;
	}

	const int isig = 0;
	const int itun = 1;
	struct pollfd pfd[2];
	memset(pfd, 0, sizeof(pfd));

	/* SIGTERM/SIGINT events cause program to end */
	pfd[isig].fd = sfd;
	pfd[isig].events = POLLIN;

	/* Input events on TUN device */
	pfd[itun].fd = tun;
	pfd[itun].events = POLLIN;


	while (!(pfd[isig].revents & POLLIN)) {
		if (poll(pfd, sizeof(pfd) / sizeof(pfd[0]), -1) < 0) {
			perror("poll");
			goto fail;
		}
		if (pfd[itun].revents & POLLIN) {
			union {
				char buf[sizeof(struct tun_frame_info) + mtu];
				struct tun_frame frame;
			} wrap;
			ssize_t len = read(tun, &wrap, sizeof(wrap));
			if (len < 0) {
				perror("read");
				goto fail;
			}
			len -= sizeof(wrap.frame.info);
			info("tun: proto=0x%04hx flags=0x%04x bytes=%zu", wrap.frame.info.proto, wrap.frame.info.flags, len);
		}
	}

	struct signalfd_siginfo ssi;

	if (read(sfd, &ssi, sizeof(ssi)) < 0) {
		perror("read");
		goto fail;
	}

	log("Stopped by signal: %s", strsignal(ssi.ssi_signo));

	goto done;
fail:
	ret = -1;
done:
	close(sfd);
	if (sigprocmask(SIG_SETMASK, &oss, NULL) < 0) {
		perror("sigprocmask");
	}
	return ret;
}
