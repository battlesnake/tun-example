tun: tun.c
	gcc -Wall -Wextra -Werror -std=gnu11 -o $@ $< -lcap-ng
	sudo setcap cap_net_admin=eip $@

.PHONY: clean
clean:
	rm -f -- tun
