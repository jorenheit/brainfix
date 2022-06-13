.PHONY: bfx bfint

all: bfx bfint
bfx: 
	make -f makefile.1
bfint:
	make -f makefile.2

clean:
	rm -f *.o interpreter/*.o

regenerate:
	bisonc++ grammar && flexc++ lexer

install: bfx bfint
	cp bfx /usr/local/bin
	cp bfint /usr/local/bin
	mkdir -p /usr/include/bfx
	cp ../std/*.bfx /usr/include/bfx
