CC=g++
CFLAGS=-c -Wall --std=c++2a #-Wfatal-errors
GENERATED_FILES=parse_bisoncpp_generated.cc lex_flexcpp_generated.cc
MY_FILES=main.cc parser.cc scanner.cc compiler.cc memory.cc bfxfunction.cc
SOURCES=$(GENERATED_FILES) $(MY_FILES)

OBJECTS=$(SOURCES:.cc=.o)
EXECUTABLE=bfx

all:	$(SOURCES) $(EXECUTABLE)

$(EXECUTABLE):	$(OBJECTS)
	$(CC) $(OBJECTS) -o $@

.cc.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o

regenerate:
	bisonc++ grammar && flexc++ lexer

bfint.cc:
interpreter: bfint.cc
	$(CC) --std=c++17 -Wall -O3 interpreter/bfint.cc -o bfint
