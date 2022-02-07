CC=g++
CFLAGS=-c -Wall --std=c++2a -Wfatal-errors
GENERATED_FILES=compiler_bisoncpp_generated.cc lex_flexcpp_generated.cc
MY_FILES=main.cc compiler.cc
SOURCES=$(GENERATED_FILES) $(MY_FILES)

OBJECTS=$(SOURCES:.cc=.o)
EXECUTABLE=bug

all:	$(SOURCES) $(EXECUTABLE)

$(EXECUTABLE):	$(OBJECTS)
	$(CC) $(OBJECTS) -o $@

.cc.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o

regenerate:
	bisonc++ grammar && flexc++ lexer

