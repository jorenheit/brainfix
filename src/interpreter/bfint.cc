#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <map>
#include <algorithm>
#include "bfint.h"

enum class CellType
    {
     INT8,
     INT16,
     INT32
    };

std::unique_ptr<BFInterpreterBase> getInterpreter(CellType type, int tapeSize, std::string const &code)
{
    switch (type)
    {
    case CellType::INT8:
        return std::make_unique<BFInterpreter<uint8_t>>(tapeSize, code);
    case CellType::INT16:
        return std::make_unique<BFInterpreter<uint16_t>>(tapeSize, code);
    case CellType::INT32:
        return std::make_unique<BFInterpreter<uint32_t>>(tapeSize, code);
    default:
        return nullptr;
    };
}

struct Options
{
    int          err{0};
    CellType     cellType{CellType::INT8};
    int          tapeLength{30000};
    std::string  bfFile;
    std::ostream *outStream{&std::cout};
    bool         random{false};
};

void printHelp(std::string const &progName)
{
    std::cout << "Usage: " << progName << " [options] <target(.bf)>\n"
              << "Options:\n"
              << "-h                  Display this text.\n"
              << "-t [Type]           Specify the number of bytes per BF-cell, where [Type] is one of\n"
                 "                    int8, int16 and int32 (int8 by default).\n"
              << "-n [N]              Specify the number of cells (30,000 by default).\n"
              << "-o [file, stdout]   Specify the output stream (defaults to stdout).\n\n"
              << "--random            Enable Random Brainf*ck extension (support ?-symbol)\n"
              << "Example: " << progName << " --random -t int16 -o output.txt program.bf\n";
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

            static std::map<std::string, CellType> const getType{
                {"int8", CellType::INT8},
                {"int16", CellType::INT16},
                {"int32", CellType::INT32}
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
                opt.tapeLength = std::stoi(args[idx + 1]);
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
        else if (args[idx] == "--random")
        {
            opt.random = true;
            ++idx;
        }
        else if (idx == args.size() - 1)
        {
            opt.bfFile = args.back();
            break;
        }
        else
        {
            std::cerr << "Unknown option " << args[idx] << ".\n";
            opt.err = 1;
            return opt;
        }
    }


    if (idx > (args.size() - 1))
    {
        std::cerr << "ERROR: No input (.bf) file specified.\n";
        opt.err = 1;
        return opt;
    }
    
    return opt;
}


int main(int argc, char **argv)
try
{
    Options opt = parseCmdLine(std::vector<std::string>(argv, argv + argc));
    if (opt.err == 1)
    {
        printHelp(argv[0]);
        return 1;
    }

    std::ifstream file(opt.bfFile);
    if (!file.is_open())
        throw std::string("File not found.");

    std::stringstream buffer;
    buffer << file.rdbuf();

    auto ptr = getInterpreter(opt.cellType, opt.tapeLength, buffer.str());
    ptr->run(std::cin, *opt.outStream, opt.random);
}
 catch (std::string const &msg)
{
    std::cerr << msg << '\n';
}
