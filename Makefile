UNAME_S  := $(shell uname -s)
CC       ?= gcc
TARGET   := fnirsi-dfu-update
SRCS     := main.c util.c

ifeq ($(UNAME_S),Darwin)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
CFLAGS  += -I$(BREW_PREFIX)/include
LDFLAGS += -L$(BREW_PREFIX)/lib
LDLIBS  := -lhidapi
else
LDLIBS  := -lhidapi-hidraw
endif

.PHONY: all linux macos build brew-install-hidapi clean

all:
ifeq ($(UNAME_S),Linux)
	@echo "Linux detected..."
	@$(MAKE) linux
else ifeq ($(UNAME_S),Darwin)
	@echo "macOS detected..."
	@$(MAKE) macos
else
	$(error Unsupported OS: $(UNAME_S))
endif

linux: build

macos: brew-install-hidapi build

build:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

brew-install-hidapi:
	@echo "Looking for hidapi..."
	@brew list -q hidapi 1>/dev/null 2>&1 || brew install hidapi

clean:
	rm -f $(TARGET)
