#include <iostream>
#include <fstream>
#include "compiler.h"

int main(int argc, char **argv) try
{
    if (argc < 2)
    {
        std::cerr << "Provide filename\n";
        return 1;
    }

    std::ifstream file(argv[1]);
    Compiler c(file);
    c.parse();
}
 catch (std::string const &msg)
 {
     std::cerr << msg << '\n';
 }
