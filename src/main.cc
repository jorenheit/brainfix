#include <fstream>
#include <stdexcept>
#include "compiler.h"

struct Options
{
    int          err{0};
    int          bytesPerCell{1};
    std::string  bfxFile;
    std::ostream *outStream{nullptr};
};

void printHelp(std::string const &progName)
{
    std::cout << "Usage: " << progName << " [options] [target(.bfx)]\n"
              << "Options:\n"
              << "-h                  display this text\n"
              << "-n [N]              specify the number of bytes per BF-cell (1 by default)\n"
              << "-o [file, stdout]   specify where the generate BF is output to\n\n"
              << "Example: " << progName << " -n 2 -o program.bf program.bfx\n";
}

Options parseCmdLine(std::vector<std::string> const &args)
{
    Options opt;
    
    size_t idx = 1;
    while (idx < (args.size() - 1))
    {
        if (args[idx] == "-h")
        {
            printHelp(args[0]);
            opt.err = 1;
            return opt;
        }

        else if (args[idx] == "-n")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'-n\'.\n";
                opt.err = 1;
                return opt;
            }
            
            try
            {
                opt.bytesPerCell = std::stoi(args[idx + 1]);
                idx += 2;
            }
            catch (std::invalid_argument const&)
            {
                std::cerr << "ERROR: Invalid argument passed to option \'-n\'\n";
                opt.err = 1;
                return opt;
            }
        }

        else if (args[idx] == "-o")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'-o\'.\n";
                opt.err = 1;
                return opt;
            }

            if (args[idx + 1] == "stdout")
            {
                opt.outStream = &std::cout;
                idx += 2;
            }
            else
            {
                std::string const &fname = args[idx + 1];
                static std::ofstream file;
                file.open(fname);
                if (!file.good())
                {
                    std::cerr << "ERROR: could not open output-file " << fname << ".\n";
                    opt.err = 1;
                    return opt;
                }

                opt.outStream = &file;
                idx += 2;
            }
        }
        else
        {
            std::cerr << "Unknown option " << args[idx] << ".\n";
            opt.err = 1;
            return opt;
        }
    }

    if (!opt.outStream || !opt.outStream->good())
    {
        std::cerr << "ERROR: Output file not set. Use -o to specifiy stdout or a file.\n";
        opt.err = 1;
        return opt;
    }

    if (idx > (args.size() - 1))
    {
        std::cerr << "ERROR: No input (.bfx) file specified.\n";
        opt.err = 1;
        return opt;
    }
    
    opt.bfxFile = args.back();
    return opt;
}


int main(int argc, char **argv) try
{
    Options opt = parseCmdLine(std::vector<std::string>(argv, argv + argc));
    if (opt.err == 1)
    {
        printHelp(argv[0]);
        return 1;
    }
    
    Compiler c(opt.bfxFile, opt.bytesPerCell);
    int err = c.compile();

    if (err) return err;

    c.write(*opt.outStream);
}
catch (std::string const &msg)
{
     std::cerr << msg << '\n';
}
