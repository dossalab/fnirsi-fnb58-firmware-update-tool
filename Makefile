UNAME_S := $(shell uname -s)

all:
ifeq ($(UNAME_S),Linux)
	@echo "Linux detected..."
	@$(MAKE) linux
else ifeq ($(UNAME_S),Darwin)
	@echo "MacOS detected..."
	@$(MAKE) macos
endif

linux:
	gcc main.c util.c -o fnirsi-dfu-update -lhidapi-hidraw

macos: brew-install-hidapi
	gcc main.c util.c -o fnirsi-dfu-update -I/opt/homebrew/include -L/opt/homebrew/lib -lhidapi

brew-install-hidapi:
	@echo "Looking for hidapi..."
	@brew list -q hidapi 1>/dev/null || brew install hidapi

clean:
	rm fnirsi-dfu-update

.PHONY: all
