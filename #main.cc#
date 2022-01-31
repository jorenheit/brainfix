#include <fstream>
#include "compiler.h"

// TODO: clean this up

int main(int argc, char **argv) try
{
    if (argc != 2)
    {
		std::cerr << "Syntax: " << argv[0] << " [file]\n";
		return 1;
    }
  
    std::ifstream file(argv[1]);
    Compiler c;
	c.compile(file);
	//c.check();
	//	p.setDebug(Parser::ACTIONCASES);
	//	p.parse();
}
 catch (std::string const &msg)
 {
	 std::cerr << msg << '\n';
 }
