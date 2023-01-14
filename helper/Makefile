BINARY      := stg_helper
ARCH        := x86
C_FLAGS     := -Wall -O2 -g
INSTALL_PATH?=/usr/local


FILES := main.c common.c

default: all
all: $(BINARY)

$(BINARY): $(FILES)
	gcc $(C_FLAGS) -o $(BINARY) $(FILES)

clean:
	rm -f $(BINARY)

install:
	cp stg_helper $(INSTALL_PATH)/bin

uninstall:
	rm $(INSTALL_PATH)/bin/$(BINARY)
