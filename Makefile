c_src := $(wildcard *.c)
cxx_src := $(wildcard *.cpp)

obj := $(c_src:%.c=%.o) $(cxx_src:%.cpp=%.oxx)

out := iplink

O ?= 0

CROSS ?=
CC := $(CROSS)gcc
CXX := $(CROSS)g++

# Use capabilities (CAP_NET_ADMIN), interferes with valgrind
SUPPORT_CAP ?= 1

WFLAGS := -Wall -Wextra -Werror
CFLAGS := $(WFLAGS) -MMD -std=gnu11 -c -O$(O)
CXXFLAGS := $(WFLAGS) -MMD -std=gnu++17 -c -O$(O)
LDFLAGS := $(WFLAGS) -O$(O)

libs :=

ifeq ($(O),0)
CFLAGS += -g
CXXFLAGS += -g
LDFLAGS += -g
else ifeq ($(O),g)
CFLAGS += -g
CXXFLAGS += -g
LDFLAGS += -g
else
CFLAGS += -s -flto -ffunction-sections -fdata-sections
CXXFLAGS += -s -flto -ffunction-sections -fdata-sections
LDFLAGS += -s -flto -Wl,--gc-sections
endif

ifeq ($(SUPPORT_CAP),1)
CFLAGS += -DSUPPORT_CAP
CXXFLAGS += -DSUPPORT_CAP
libs += cap-ng
endif

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
	$(CXX) $(LDFLAGS) -o $@ $^ $(addprefix -l,$(libs))
ifeq ($(SUPPORT_CAP),1)
	sudo setcap cap_net_admin=eip $@
endif

$(tmp)/%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<

$(tmp)/%.oxx: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

-include $(wildcard $(tmp)/*.d)
