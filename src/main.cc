#include <fstream>
#include "compiler.h"

int main(int argc, char **argv) try
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "Syntax: " << argv[0] << " bfx-file [bf-file]\n"
                  << "If no bf-file is provided, output will be sent to stdout\n";
        return 1;
    }

    Compiler c(argv[1]);
    int err = c.compile();

    if (err) return err;
    
    if (argc == 2)
        c.write();
    else
    {
        std::ofstream ofile(argv[2]);
        c.write(ofile);
    }
}
 catch (std::string const &msg)
 {
     std::cerr << msg << '\n';
 }
