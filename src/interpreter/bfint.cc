#include <iostream>
#include <vector>
#include <stack>
#include <fstream>
#include <sstream>

template <typename CellType = uint8_t>
class BFInterpreter
{
    std::vector<CellType> d_array;
    std::string d_code;

    size_t d_arrayPointer{0};
    size_t d_codePointer{0};
    std::stack<int> d_loopStack;

    enum Ops: char
        {
         PLUS  = '+',
         MINUS = '-',
         LEFT  = '<',
         RIGHT = '>',
         START_LOOP = '[',
         END_LOOP = ']',
         PRINT = '.',
         READ = ',',
        };
  
public:
    BFInterpreter(size_t arraySize, std::string code):
        d_array(arraySize),
        d_code(code)
    {}

    void run(std::istream &in = std::cin, std::ostream &out = std::cout)
    {
        while (true)
        {
            char token = d_code[d_codePointer];
            switch (token)
            {
            case LEFT: pointerDec(); break;
            case RIGHT: pointerInc(); break;
            case PLUS: plus(); break;
            case MINUS: minus(); break;
            case PRINT: print(out); break;
            case READ: read(in); break;
            case START_LOOP: startLoop(); break;
            case END_LOOP: endLoop(); break;
            default: break;
            }

            if (++d_codePointer >= d_code.size())
                break;
        }
    }
  
    void plus()
    {
        ++d_array[d_arrayPointer];
    }

    void minus()
    {
        // if (d_array[d_arrayPointer] == 0)
        //  std::cerr << "Warning: about to decrement a 0\n";
        
        --d_array[d_arrayPointer];
    }

    void pointerInc()
    {
        if (++d_arrayPointer >= d_array.size())
        {
            d_array.resize(2 * d_array.size());
        }
    }

    void pointerDec()
    {
        if (d_arrayPointer == 0)
            throw std::string("Error: trying to decrement pointer beyond beginning.");

        --d_arrayPointer;
    }

    void startLoop()
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

    void endLoop()
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

    void print(std::ostream &out)
    {
        out << (char)d_array[d_arrayPointer];
    }

    void read(std::istream &in)
    {
        char c;
        in.get(c);
        d_array[d_arrayPointer] = c;
    }

    void printState()
    {
        for (auto x: d_array)
            std::cout << (int)x << ' ';
        std::cout << '\n';
    }
};



int main(int argc, char **argv)
try
{
    if (argc != 3)
    {
        std::cerr << "Syntax: " << argv[0] << " [tape-length] [filename]\n";
        return 1;
    }

    size_t len = std::stoi(argv[1]);
    std::ifstream file(argv[2]);
    std::stringstream buffer;
    buffer << file.rdbuf();

    BFInterpreter<unsigned> bf(len, buffer.str());
    bf.run(std::cin, std::cout);
    //  bf.printState();
}
 catch (std::string const &msg)
{
    std::cerr << msg << '\n';
}
