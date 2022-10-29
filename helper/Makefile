BINARY      := stg_helper
ARCH        := x86
C_FLAGS     := -Wall -O0 -g

FILES := main.c common.c

default: all
all: $(BINARY)

$(BINARY): $(FILES)
	gcc $(C_FLAGS) -o $(BINARY) $(FILES)

clean:
	rm -f $(BINARY)