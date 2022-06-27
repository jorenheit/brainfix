#include <cassert>
#include <chrono>
#include <csignal>
#include <cstring> // debug

#ifdef USE_CURSES
#include <ncurses.h>
#endif

#include "bfint.h"

namespace _MaxInt
{
    template <typename T>
    constexpr size_t _getMax()
    {
        return (static_cast<size_t>(1) << (8 * sizeof(T))) - 1;
    }
    
    inline static size_t get(CellType c)
    {
        switch (c)
        {
        case CellType::INT8:  return _getMax<uint8_t>();
        case CellType::INT16: return _getMax<uint16_t>();
        case CellType::INT32: return _getMax<uint32_t>();;
        }
        throw -1;
    }
}

BFInterpreter::BFInterpreter(size_t arraySize, std::string const &code, CellType const type):
    d_array(arraySize),
    d_code(code),
    d_cellType(type),
    d_uniformDist(0, _MaxInt::get(type))
{
    auto t0 = std::chrono::system_clock::now().time_since_epoch();
    auto ms = duration_cast<std::chrono::milliseconds>(t0).count();
    d_rng.seed(ms);
}
    
void BFInterpreter::run(std::istream &in, std::ostream &out,
                        bool const randEnabled, bool const randomWarning
#ifdef USE_CURSES
                        , bool const gamingMode
#endif
                        )
{
#ifdef USE_CURSES
    // Setup ncurses window
    if (gamingMode)
    {
        auto win = initscr();
        scrollok(win, true);
        cbreak();
        noecho();
        nonl();
        nodelay(stdscr, TRUE);
        curs_set(0);

        signal(SIGINT, [](int sig)
                       {
                           finish(sig);
                       });
    }
#endif        
        
    while (true)
    {
        char token = d_code[d_codePointer];
        switch (token)
        {
        case LEFT: pointerDec(); break;
        case RIGHT: pointerInc(); break;
        case PLUS: plus(); break;
        case MINUS: minus(); break;
        case PRINT:
            {
                if (gamingMode)
                    printCurses(out);
                else
                    print(out);
                break;
            }
        case READ:
            {
                if (gamingMode)
                    readCurses(in);
                else
                    read(in);
                break;
            }
        case START_LOOP: startLoop(); break;
        case END_LOOP: endLoop(); break;
        case RAND:
            {
                static bool warned = false;
                if (randEnabled)
                    random();
                else if (randomWarning && !warned)
                {
                    std::cerr << "\n"
                        "=========================== !!!!!! ==============================\n"
                        "Warning: BF-code contains '?'-commands, which may be\n"
                        "interpreted as the random-operation, an extension to the\n"
                        "canonical BF instructionset. This extension can be enabled\n"
                        "with the --random option.\n"
                        "This warning can be disabled with the --no-random-warning option.\n"
                        "=========================== !!!!!! ==============================\n";
                    warned = true;
                }
                break;
            }
        default: break;
        }

        if (++d_codePointer >= d_code.size())
            break;
    }

    if (gamingMode)
    {
        nodelay(stdscr, false);
        getch();
        finish(0);
    }
}

int BFInterpreter::consume(Ops op)
{
    assert(d_code[d_codePointer] == op && "codepointer should be pointing at op now");
        
    int n = 1;
    while (d_code[d_codePointer + n] == op)
        ++n;

    d_codePointer += (n - 1);
    return n;
}
    
void BFInterpreter::plus()
{
    int const n = consume(PLUS);
    switch (d_cellType)
    {
    case CellType::INT8:
        d_array[d_arrayPointer] = static_cast<uint8_t>(d_array[d_arrayPointer] + n);
        break;
    case CellType::INT16:
        d_array[d_arrayPointer] = static_cast<uint16_t>(d_array[d_arrayPointer] + n);
        break;
    case CellType::INT32:
        d_array[d_arrayPointer] = static_cast<uint32_t>(d_array[d_arrayPointer] + n);
        break;
    }
}
    
void BFInterpreter::minus()
{
    int const n = consume(MINUS);
    switch (d_cellType)
    {
    case CellType::INT8:
        d_array[d_arrayPointer] = static_cast<uint8_t>(d_array[d_arrayPointer] - n);
        break;
    case CellType::INT16:
        d_array[d_arrayPointer] = static_cast<uint16_t>(d_array[d_arrayPointer] - n);
        break;
    case CellType::INT32:
        d_array[d_arrayPointer] = static_cast<uint32_t>(d_array[d_arrayPointer] - n);
        break;
    }
}

void BFInterpreter::pointerInc()
{
    int const n = consume(RIGHT);
    d_arrayPointer += n;

    while (d_arrayPointer >= d_array.size())
        d_array.resize(2 * d_array.size());
}

void BFInterpreter::pointerDec()
{
    if (d_arrayPointer == 0)
        throw std::string("Error: trying to decrement pointer beyond beginning.");

    int const n = consume(LEFT);
    d_arrayPointer -= n;
}

void BFInterpreter::startLoop()
{
    if (d_array[d_arrayPointer] != 0)
    {    
        d_loopStack.push(d_codePointer);
    }
    else
    {
        int bracketCount = 1;
        while (bracketCount != 0 && d_codePointer < d_code.size())
        {
            ++d_codePointer;
            if (d_code[d_codePointer] == START_LOOP)
                ++bracketCount;
            else if (d_code[d_codePointer] == END_LOOP)
                --bracketCount;
        }
    }
}

void BFInterpreter::endLoop()
{
    if (d_array[d_arrayPointer] != 0)
    {
        d_codePointer = d_loopStack.top();
    }
    else
    {
        d_loopStack.pop();
    }
}

void BFInterpreter::print(std::ostream &out)
{
    out << (char)d_array[d_arrayPointer] << std::flush;
}

void BFInterpreter::printCurses(std::ostream &out)
{
#ifdef USE_CURSES
    static char const ESC = 27; // Control char
    static std::string ansiBuffer;
    
    char const c = d_array[d_arrayPointer];
    if (c == ESC)
    {
        if (ansiBuffer.empty())
        {
            ansiBuffer.push_back(c);
        }
        else
        {
            handleAnsi(ansiBuffer, true);
            ansiBuffer.push_back(c);
        }
    }
    else
    {
        if (ansiBuffer.empty())
        {
            addch(c);
        }
        else
        {
            ansiBuffer.push_back(c);
            handleAnsi(ansiBuffer, false);
        }
    }
        
    refresh();
#else
    assert(false && "printCurses() called but not compiled with USE_CURSES");
#endif        
}

void BFInterpreter::handleAnsi(std::string &ansiStr, bool const force)
{
#ifdef USE_CURSES
    static char const ESC = 27; // Control char
    assert(ansiStr.length() > 1 && "handleAnsi called with less than 2 characters");
    assert(ansiStr[0] == ESC && "handleAnsi called on string not starting with ESC");

    auto const flush = [&]()
                       {
                           addstr(ansiStr.c_str());
                           ansiStr.clear();
                       };
    
    if (ansiStr.length() == 2 && ansiStr[1] != '[')
    {
        // ESC not followed by '[' -> not ansi
        flush();
        return;
    }
    
    if (ansiStr.length() < 3)
    {
        if (force)
            flush();
        
        return; // cannot be a complete ansi escape sequence
    }

    // 3 or more characters present
    int row, col;
    getyx(stdscr, row, col);

    bool handled = true;
    switch (ansiStr.back())
    {
    case 'A': // cursor up
        {
            int n = std::stoi(ansiStr.substr(2, ansiStr.length() - 3));
            if (row)
            {
                row = std::max(0, row - n);
                move(row, col);
            }
            break;
        }
    case 'B': // cursor down
        {
            int n = std::stoi(ansiStr.substr(2, ansiStr.length() - 3));
            move(row + n, col);
            break;
        }
    case 'C': // cursor right
        {
            int n = std::stoi(ansiStr.substr(2, ansiStr.length() - 3));
            move(row, col + n);
            break;
        }
    case 'D': // cursor left
        {
            int n = std::stoi(ansiStr.substr(2, ansiStr.length() - 3));
            if (col)
            {
                col = std::max(0, col - n);
                move(row, col);
                clrtoeol();
            }
            break;
        }
    case 'H':
        {
            if (ansiStr.length() == 3)
            {
                move(0, 0);
                break;
            }
            
            size_t const separator = ansiStr.find(';');
            if (separator == std::string::npos)
            {
                flush();
                break;
            }

            row = std::stoi(ansiStr.substr(2, separator - 2)) - 1;
            col = std::stoi(ansiStr.substr(separator + 1, ansiStr.length() - separator - 2)) - 1;
            move(row, col);
            break;
        }
    case 'K':
        {
            int n = (ansiStr.length() == 3) ? 0 :
                std::stoi(ansiStr.substr(2, ansiStr.length() - 3));

            switch (n)
            {
            case 0:
                {
                    clrtoeol();
                    break;
                }
            case 1:
                {
                    move(row, 0);
                    addstr(std::string(col, ' ').c_str());
                    break;
                }
            case 2:
                {
                    move(row, 0);
                    clrtoeol();
                    move(row, col);
                    break;
                }
            default:
                {
                    handled = false;
                }
            }
            break;
        }
    case 'J':
        {
            int n = (ansiStr.length() == 3) ? 0 :
                std::stoi(ansiStr.substr(2, ansiStr.length() - 3));
            
            switch (n)
            {
            case 0:
                {
                    clrtobot();
                    break;
                }
            case 1:
                {
                    for (int i = 0; i <= row; ++i)
                    {
                        move(i, 0);
                        clrtoeol();
                    }
                    move(row, col);
                    break;
                }
            case 2:
                {
                    move(0,0);
                    clrtobot();
                    break;
                }
            default:
                {
                    handled = false;
                }   
            }
            break;
        }
    default:
        {
            handled = false;
        }
    }
    
    if (handled)
    {
        ansiStr.clear();
        return;
    }
    
    // ANSI sequence not yet terminated. Check if character is allowed
    if (std::string("0123456789;").find(ansiStr.back()) == std::string::npos || force)
        flush();

    // Still going. Don't do anything and wait for next call
#else
    assert(false && "handleAnsi called without USE_CURSES defined");
#endif
}

void BFInterpreter::read(std::istream &in)
{
    char c;
    in.get(c);
    d_array[d_arrayPointer] = c;
}

void BFInterpreter::readCurses(std::istream &out)
{ 
#ifdef USE_CURSES       
    int c = getch();
    d_array[d_arrayPointer] = (c < 0) ? 0 : static_cast<char>(c);
#else
    assert(false && "readCurses() called but not compiled with USE_CURSES");
#endif        
}

void BFInterpreter::random()
{
    auto val = d_uniformDist(d_rng);
    d_array[d_arrayPointer] = val;
}

void BFInterpreter::printState()
{
    for (auto x: d_array)
        std::cout << (int)x << ' ';
    std::cout << '\n';
}

void BFInterpreter::finish(int sig)
{
    endwin();
    if (sig == SIGINT)
        exit(0);
}
