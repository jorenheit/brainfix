# Set default BFX include path and install path
INSTALL_PATH=/usr/local/bin
BFX_INCLUDE=/usr/local/include/bfx

# Compile with gaming mode available? Requires ncurses
GAMING_MODE_AVAILABLE=1

.PHONY: bfx bfint

all: bfx bfint
bfx:
	make -C src -f makefile.1 BFX_DEFAULT_INCLUDE_PATH=$(BFX_INCLUDE)
bfint:
	make -C src -f makefile.2 GAMING_MODE_AVAILABLE=$(GAMING_MODE_AVAILABLE)

clean:
	rm -f src/*.o src/interpreter/*.o

regenerate:
	cd src && bisonc++ grammar && flexc++ lexer

install: bfx bfint
	cp bfx $(INSTALL_PATH)
	cp bfint $(INSTALL_PATH)
	mkdir -p $(BFX_INCLUDE)
	cp std/std.bfx $(BFX_INCLUDE)
	cp std/stdio.bfx $(BFX_INCLUDE)
	cp std/stdmath.bfx $(BFX_INCLUDE)
	cp std/stdbool.bfx $(BFX_INCLUDE)
	cp std/stdstring.bfx $(BFX_INCLUDE)

uninstall:
	rm -f $(INSTALL_PATH)/bfx
	rm -f $(INSTALL_PATH)/bfint
	rm -rf $(BFX_INCLUDE)
