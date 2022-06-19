#include <fstream>
#include <stdexcept>
#include <map>
#include <algorithm>
#include "compiler.h"


void printHelp(std::string const &progName)
{
    std::cout << "Usage: " << progName << " [options] <target(.bfx)>\n"
              << "Options:\n"
              << "-h                  Display this text.\n"
              << "-t [Type]           Specify the number of bytes per BF-cell, where [Type] is one of\n"
                 "                    int8, int16 and int32 (int8 by default).\n"
              << "-I [path to folder] Specify additional include-path.\n"
              << "                      This option may appear multiple times to specify multiple folders.\n"
              << "-O0                 Do NOT do any constant expression evaluation.\n"
              << "-O1                 Do constant expression evaluation (default).\n"
              << "--max-unroll-iterations [N]\n"
              << "                    Specify the maximum number of loop-iterations that will be unrolled.\n"
              << "                      Defaults to 20.\n"
              << "--random            Enable random number generation (generates the ?-symbol).\n"
              << "                      Your interpreter must support this extension!\n"
              << "--profile [file]    Write the memory profile to a file. In this file, the number of visits\n"
              << "                      to each of the cells is listed. It can for example be used to inspec\n"
              << "                      the total number of cells used by the program."  
              << "--no-bcr            Disable break/continue/return statements for more compact output.\n"
              << "--no-multiple-inclusion-warning\n"
              << "                    Do not warn when a file is included more than once, or when files \n"
              << "                      with duplicate names are included.\n"
              << "--no-assert-warning\n"
              << "                    Do not warn when static_assert is used in non-constant context.\n"
              << "-o [file, stdout]   Specify the output stream/file (default stdout).\n\n"
              << "Example: " << progName << " -o program.bf -O1 -I ~/my_bfx_project -t int16 program.bfx\n";
}

std::pair<Compiler::Options, int> parseCmdLine(std::vector<std::string> const &args)
{
    Compiler::Options opt;

    size_t idx = 1;
    while (idx < args.size())
    {
        if (args[idx] == "-h")
        {
            return {opt, 1};
        }
        else if (args[idx] == "-t")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'-t\'.\n";
                return {opt, 1};
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
                return {opt, 1};
            }
        }
        else if (args[idx] == "-I")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'-I\'.\n";
                return {opt, 1};
            }
            
            opt.includePaths.push_back(args[idx + 1]);
            idx += 2;
        }
        else if (args[idx] == "-o")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'-o\'.\n";
                return {opt, 1};
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
                    return {opt, 1};
                }

                opt.outStream = &file;
                idx += 2;
            }
        }
        else if (args[idx] == "-O0")
        {
            opt.constEvalEnabled = false;
            ++idx;
        }
        else if (args[idx] == "-O1")
        {
            ++idx;
        }
        else if (args[idx] == "--max-unroll-iterations")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No argument passed to option \'--max-unroll-iterations\'.\n";
                return {opt, 1};
            }
            
            try {
                opt.maxUnrollIterations = std::stoi(args[idx + 1]);
                idx += 2;
            }
            catch (...) {
                std::cerr << "Could not convert argument to --max-unroll-iterations to integer.";
                return {opt, 1};
            }
        }
        else if (args[idx] == "--random")
        {
            opt.randomEnabled = true;
            ++idx;
        }
        else if (args[idx] == "--profile")
        {
            if (idx == args.size() - 1)
            {
                std::cerr << "ERROR: No filename passed to option \'--profile\'.\n";
                return {opt, 1};
            }

            opt.moveLogFile = args[idx + 1];
            idx += 2;

        }
        else if (args[idx] == "--no-bcr")
        {
            opt.bcrEnabled = false;
            ++idx;
        }
        else if (args[idx] == "--no-multiple-inclusion-warning")
        {
            opt.includeWarningEnabled = false;
            ++idx;
        }
        else if (args[idx] == "--no-assert-warning")
        {
            opt.assertWarningEnabled = false;
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
            return {opt, 1};
        }
    }

    if (!opt.outStream || !opt.outStream->good())
    {
        std::cerr << "ERROR: Output file not set. Use -o to specifiy stdout or a file.\n";
        return {opt, 1};
    }

    if (idx > (args.size() - 1))
    {
        std::cerr << "ERROR: No input (.bfx) file specified.\n";
        return {opt, 1};
    }

    return {opt, 0};
}


int main(int argc, char **argv) try
{
    auto result = parseCmdLine(std::vector<std::string>(argv, argv + argc));
    if (result.second == 1)
    {
        printHelp(argv[0]);
        return 1;
    }
    
    Compiler c(result.first);
    int err = c.compile();

    if (err) return err;

    c.write();
}
catch (std::string const &msg)
{
     std::cerr << msg << '\n';
}
