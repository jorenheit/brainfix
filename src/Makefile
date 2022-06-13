# Set default BFX include path
INSTALL_PATH=/usr/local/bin
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
	cp bfx $(INSTALL_PATH)
	cp bfint $(INSTALL_PATH)
	mkdir -p $(BFX_INCLUDE)
	cp ../std/std.bfx $(BFX_INCLUDE)
	cp ../std/stdio.bfx $(BFX_INCLUDE)
	cp ../std/stdmath.bfx $(BFX_INCLUDE)
	cp ../std/stdbool.bfx $(BFX_INCLUDE)

uninstall:
	rm -f $(INSTALL_PATH)/bfx
	rm -f $(INSTALL_PATH)/bfint
	rm -rf $(BFX_INCLUDE)
