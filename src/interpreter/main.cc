#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <map>
#include <algorithm>
#include <ctime>
#include <unistd.h>
#include "bfint.h"

struct Options
{
    int          err{0};
    CellType     cellType{CellType::INT8};
    int          tapeLength{30000};
    std::string  bfFile;
    std::ostream *outStream{&std::cout};
    bool         random{false};
    bool         gaming{false};
    bool         randomWarning{true};
};

void printHelp(std::string const &progName)
{
    std::cout << "Usage: " << progName << " [options] <target(.bf)>\n"
              << "Options:\n"
              << "-h, --help          Display this text.\n"
              << "-t, --type [Type]   Specify the number of bytes per BF-cell, where [Type] is one of\n"
                 "                    int8, int16 and int32 (int8 by default).\n"
              << "-n [N]              Specify the number of cells (30,000 by default).\n"
              << "-o [file, stdout]   Specify the output stream (defaults to stdout).\n\n"
#ifdef USE_CURSES        
              << "--gaming            Enable gaming-mode.\n"
              << "--gaming-help       Display additional information about gaming-mode.\n"
#endif        
              << "--random            Enable Random Brainf*ck extension (support ?-symbol)\n"
              << "--no-random-warning Don't display a warning when ? occurs without running --random.\n\n"
              << "Example: " << progName << " --random -t int16 -o output.txt program.bf\n";
}

#ifdef USE_CURSES
void printGamingHelp(std::string const &progName)
{
    std::cout <<
        "When " << progName << " is run with the --gaming option, all writes and reads are performed\n"
        "by ncurses, in order to establish non-blocking IO. This allowes you to run games written in\n"
        "BF that require keyboard-input (',' in BF) to be processed immediately, without waiting for\n"
        "the user to press enter. If no key was pressed, a 0 is stored to the current BF-cell.\n\n"
        
        "In the default non-gaming mode, it is possible to write ANSI escape sequences to the output,\n"
        " which may be used to modify the cursor position, clear the screen, or change the color. A\n"
        "subset of these sequences has been implemented and will be translated to sequences of\n"
        "ncurses-calls to mimic this behavior:\n\n"
        "  - ESC[nA    ==> Move the cursor up n lines.\n"
        "  - ESC[nB    ==> Move the cursor down n lines.\n"
        "  - ESC[nC    ==> Move the cursor right n steps.\n"
        "  - ESC[nD    ==> Move the cursor left n steps (erasing present characters).\n"
        "  - ESC[n;mH  ==> Move the cursor to row n, column m.\n"
        "  - ESC[H     ==> Move the cursor to the top-left of the screen.\n"
        "  - ESC[nK    ==> n = 0: clear from cursor to end-of-line.\n"
        "                  n = 1: clear from cursor to start-of-line.\n"
        "                  n = 2: clear the entire line\n"
        "  - ESC[nJ    ==> n = 0: clear from cursor to bottom of screen.\n"
        "              ==> n = 1: clear all lines above cursor, including current line.\n"
        "              ==> n = 2: clear entire screen.\n\n";
}
#endif

Options parseCmdLine(std::vector<std::string> const &args)
{
    Options opt;
    
    size_t idx = 1;
    while (idx < args.size())
    {
        if (args[idx] == "-h" || args[idx] == "--help")
        {
            opt.err = 1;
            return opt;
        }

        else if (args[idx] == "-t" || args[idx] == "--type")
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
        else if (args[idx] == "--gaming")
        {
            opt.gaming = true;
            ++idx;
        }
        else if (args[idx] == "--gaming-help")
        {
            opt.gaming = true;
            opt.err = 1;
            return opt;
        }
        else if (args[idx] == "--no-random-warning")
        {
            opt.randomWarning = false;
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
#ifdef USE_CURSES        
        if (opt.gaming)
            printGamingHelp(argv[0]);
        else
            printHelp(argv[0]);
#else
        printHelp(argv[0]);
#endif        
        return 1;
    }

    std::ifstream file(opt.bfFile);
    if (!file.is_open())
        throw std::string("File not found: ") + opt.bfFile;

    std::stringstream buffer;
    buffer << file.rdbuf();

    BFInterpreter bfint(opt.tapeLength, buffer.str(), opt.cellType);
    bfint.run(std::cin, *opt.outStream, opt.random, opt.randomWarning, opt.gaming);
}
 catch (std::string const &msg)
{
    std::cerr << msg << '\n';
}
