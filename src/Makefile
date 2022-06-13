# Set default BFX include path
BFX_INCLUDE=/usr/include/bfx

.PHONY: bfx bfint

all: bfx bfint
bfx:
	make -f makefile.1 BFX_DEFAULT_INCLUDE_PATH=$(BFX_INCLUDE)
bfint:
	make -f makefile.2

clean:
	rm -f *.o interpreter/*.o

regenerate:
	bisonc++ grammar && flexc++ lexer

install: bfx bfint
	cp bfx /usr/local/bin
	cp bfint /usr/local/bin
	mkdir -p $(BFX_INCLUDE)
	cp ../std/std.bfx $(BFX_INCLUDE)
	cp ../std/stdio.bfx $(BFX_INCLUDE)
	cp ../std/stdmath.bfx $(BFX_INCLUDE)
	cp ../std/stdbool.bfx $(BFX_INCLUDE)

uninstall:
	rm /usr/local/bin/bfx 2> /dev/null
	rm /usr/local/bin/bfint 2> /dev/null
	rm -rf $(BFX_INCLUDE)
