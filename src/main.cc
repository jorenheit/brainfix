#include <fstream>
#include <stdexcept>
#include <map>
#include <algorithm>
#include "compiler.h"

struct Options
{
    int                       err{0};
    Compiler::CellType        cellType{Compiler::CellType::INT8};
    std::vector<std::string>  includePaths;
    std::string               bfxFile;
    std::ostream*             outStream{nullptr};
    bool                      constEval{true};
};

void printHelp(std::string const &progName)
{
    std::cout << "Usage: " << progName << " [options] [target(.bfx)]\n"
              << "Options:\n"
              << "-h                  Display this text.\n"
              << "-t [Type]           Specify the number of bytes per BF-cell, where [Type] is one of\n"
                 "                    int8, int16 and int32 (int8 by default).\n"
              << "-I [path to folder] Specify additional include-path.\n"
              << "                    This option may appear multiple times to specify multiple folders.\n"
              << "-O0                 Do NOT do any constant expression evaluation.\n"
              << "-O1                 Do constant expression evaluation (default).\n "
              << "-o [file, stdout]   Specify where the generate BF is output to.\n\n"
              << "Example: " << progName << " -o program.bf -I ../std/ -t int16 program.bfx\n";
}

Options parseCmdLine(std::vector<std::string> const &args)
{
    Options opt;
    
    size_t idx = 1;
    while (idx < args.size())
    {
        if (args[idx] == "-h")
        {
            opt.err = 1;
            return opt;
        }
        else if (args[idx] == "-t")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'-t\'.\n";
                opt.err = 1;
                return opt;
            }

            static std::map<std::string, Compiler::CellType> const getType{
                {"int8", Compiler::CellType::INT8},
                {"int16", Compiler::CellType::INT16},
                {"int32", Compiler::CellType::INT32}
            };

            auto tolower = [](std::string str)->std::string
                           {
                               std::transform(str.begin(), str.end(), str.begin(),
                                              [](unsigned char c){ return std::tolower(c); });
                               return str;
                           };

            std::string arg = tolower(args[idx + 1]);
            try
            {
                opt.cellType = getType.at(arg);
                idx += 2;
            }
            catch (std::out_of_range const&)
            {
                std::cerr << "ERROR: Invalid argument passed to option \'-t\'\n";
                opt.err = 1;
                return opt;
            }
        }
        else if (args[idx] == "-I")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'-I\'.\n";
                opt.err = 1;
                return opt;
            }
            
            opt.includePaths.push_back(args[idx + 1]);
            idx += 2;
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
        else if (args[idx] == "-O0")
        {
            opt.constEval = false;
            ++idx;
        }
        else if (args[idx] == "-O1")
        {
            ++idx;
        }
        else if (idx == args.size() - 1)
        {
            opt.bfxFile = args.back();
            break;
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
    
    Compiler c(opt.bfxFile, opt.cellType, opt.includePaths, opt.constEval);
    int err = c.compile();

    if (err) return err;

    c.write(*opt.outStream);
}
catch (std::string const &msg)
{
     std::cerr << msg << '\n';
}
