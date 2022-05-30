#ifndef BFINT_H
#define BFINT_H

#include <vector>
#include <stack>
#include <cassert>
#include <random>
#include <chrono>

class BFInterpreterBase
{
    
protected:
    std::vector<int> d_array;
    std::string d_code;

public:
    BFInterpreterBase(size_t arraySize, std::string const &code):
        d_array(arraySize),
        d_code(code)
    {}
    
    virtual void run(std::istream &in = std::cin, std::ostream &out = std::cout, bool randEnabled = false) = 0;
};

template <typename CellType>
class BFInterpreter: public BFInterpreterBase
{
    using RngType = std::mt19937;
    std::uniform_int_distribution<RngType::result_type> d_uniformDist;
    RngType d_rng;

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
         RAND = '?',
        };

public:
    template <typename ... Args>
    BFInterpreter(Args&& ... args):
        BFInterpreterBase(std::forward<Args>(args)...),
        d_uniformDist(0, std::numeric_limits<CellType>::max())
    {
        auto t0 = std::chrono::system_clock::now().time_since_epoch();
        auto ms = duration_cast<std::chrono::milliseconds>(t0).count();
        d_rng.seed(ms);
    }
    
    virtual void run(std::istream &in = std::cin, std::ostream &out = std::cout, bool const randEnabled = false) override
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
            case RAND:
                {
                    if (randEnabled)
                        random();
                    break;
                }
            default: break;
            }

            if (++d_codePointer >= d_code.size())
                break;
        }
    }

private:
    int consume(Ops op)
    {
        assert(d_code[d_codePointer] == op && "codepointer should be pointing at op now");
        
        int n = 1;
        while (d_code[d_codePointer + n] == op)
            ++n;

        d_codePointer += (n - 1);
        return n;
    }
    
    void plus()
    {
        int const n = consume(PLUS);
        d_array[d_arrayPointer] = static_cast<CellType>(d_array[d_arrayPointer] + n);
    }
    
    void minus()
    {
        int const n = consume(MINUS);
        d_array[d_arrayPointer] = static_cast<CellType>(d_array[d_arrayPointer] - n);
    }

    void pointerInc()
    {
        int const n = consume(RIGHT);
        d_arrayPointer += n;

        while (d_arrayPointer >= d_array.size())
            d_array.resize(2 * d_array.size());
    }

    void pointerDec()
    {
        if (d_arrayPointer == 0)
            throw std::string("Error: trying to decrement pointer beyond beginning.");

        int const n = consume(LEFT);
        d_arrayPointer -= n;
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
        out << (char)d_array[d_arrayPointer] << std::flush;
    }

    void read(std::istream &in)
    {
        char c;
        in.get(c);
        d_array[d_arrayPointer] = c;
    }

    void random()
    {
        auto val = d_uniformDist(d_rng);
        d_array[d_arrayPointer] = val;
    }

    void printState()
    {
        for (auto x: d_array)
            std::cout << (int)x << ' ';
        std::cout << '\n';
    }
};



#endif
