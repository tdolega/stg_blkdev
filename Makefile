compile:
	cd helper && make clean && make
	cd module && make clean && make

install:
	cd helper && sudo make install
	cd module && sudo make install

all: compile install
