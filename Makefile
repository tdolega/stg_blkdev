compile:
	cd helper && make clean && make
	cd module && make clean && make

install:
	cd helper && sudo make install
	cd module && sudo make install

uninstall:
	cd helper && sudo make uninstall
	cd module && sudo make uninstall

all: compile install
