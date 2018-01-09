src := $(wildcard *.c)
obj := $(src:%.c=%.o)
out := tun

O ?= 0

WFLAGS := -Wall -Wextra -Werror
CFLAGS := $(WFLAGS) -MMD -std=gnu11 -c -O$(O)
LDFLAGS := $(WFLAGS) -O$(O)

libs := cap-ng

tmp := .tmp/$(O)
bin := .bin/$(O)

$(shell rm -f bin tmp)
$(shell mkdir -p .bin/$(O) .tmp/$(O))
$(shell ln -s .bin/$(O) bin)
$(shell ln -s .tmp/$(O) tmp)

.PHONY: all
all: $(addprefix $(bin)/,$(out))

.PHONY: clean
clean:
	rm -rf -- .tmp .bin
	rm -f -- tmp bin

$(addprefix $(bin)/,$(out)): $(addprefix $(tmp)/,$(obj))
	$(CC) $(LDFLAGS) -o $@ $^ $(addprefix -l,$(libs))
	sudo setcap cap_net_admin=eip $@

$(tmp)/%.o: %.c
	gcc $(CFLAGS) -o $@ $<

-include $(wildcard $(tmp)/*.d)
