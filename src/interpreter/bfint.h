#ifndef BFINT_H
#define BFINT_H

#include <vector>
#include <stack>
#include <random>
#include <iostream>

enum class CellType
    {
     INT8,
     INT16,
     INT32
    };


class BFInterpreter
{
    std::vector<int> d_array;
    std::string d_code;
    size_t d_arrayPointer{0};
    size_t d_codePointer{0};
    std::stack<int> d_loopStack;
    CellType const d_cellType;

    using RngType = std::mt19937;
    std::uniform_int_distribution<RngType::result_type> d_uniformDist;
    RngType d_rng;

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
    BFInterpreter(size_t arraySize, std::string const &code, CellType const type);
    void run(std::istream &in, std::ostream &out,
             bool const randEnabled, bool const randomWarning
#ifdef USE_CURSES             
             , bool const gamingMode
#endif             
             );       

private:
    int consume(Ops op);
    void plus();
    void minus();
    void pointerInc();
    void pointerDec();
    void startLoop();
    void endLoop();
    void print(std::ostream &out);
    void printCurses(std::ostream &out);
    void read(std::istream &in);
    void readCurses(std::istream &out);
    void random();
    void printState();
    void handleAnsi(std::string &ansiStr, bool const force);
    static void finish(int sig);
};



#endif
