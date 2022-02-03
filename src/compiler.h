#ifndef COMPILER_H
#define COMPILER_H

#include <string>
#include <map>
#include <deque>
#include <stack>
#include <sstream>

#include "memory.h"
#include "parser.h"

class Compiler
{
    friend class Parser;
    
    static constexpr int TAPE_SIZE_INITIAL = 1000;
    static constexpr int MAX_ARRAY_SIZE    = 250;

    Parser d_parser;
    size_t d_pointer{0};
    Memory d_memory;

    std::map<std::string, BFXFunction>  d_functionMap;
    std::map<std::string, uint8_t>      d_constMap;
    std::ostringstream                  d_codeBuffer;
    std::stack<int>                     d_stack;


    class ScopeStack
    {
        template <typename T>
        using StackType = std::deque<T>;

        StackType<std::pair<std::string, StackType<int>>> d_stack;
        
    public:
        bool empty() const;
        std::string currentFunction() const;
        std::string currentScopeString() const;
        bool containsFunction(std::string const &name) const;
        void pushFunctionScope(std::string const &name);
        void pushSubScope();
        std::string popFunctionScope();
        std::string popSubScope();
    };

    ScopeStack d_scopeStack;

    
    enum class Stage
        {
         PARSING,
         CODEGEN,
         FINISHED
        };

    Stage       d_stage;
    std::string d_instructionFilename;
    int         d_instructionLineNr;

    
public:
    Compiler(std::string const &file):
        d_parser(file, *this),
        d_memory(TAPE_SIZE_INITIAL)
    {}

    int compile();
    void write(std::ostream &out = std::cout);

private:
    void addFunction(BFXFunction const &bfxFunc);
    void addGlobals(std::vector<std::pair<std::string, int>> const &declarations);
    void addConstant(std::string const &ident, uint8_t const num);

    uint8_t compileTimeConstant(std::string const &ident) const;
    bool    isCompileTimeConstant(std::string const &ident) const;

    static bool validateInlineBF(std::string const &ident);
    static std::string cancelOppositeCommands(std::string const &bf);
    
    // Memory management uitilities
    int  allocate(std::string const &ident, int const sz = 1);
    int  allocateTemp(int const sz = 1);
    int  allocateTempBlock(int const sz);
    int  addressOf(std::string const &ident);
    void pushStack(int const addr);
    int  popStack();

    // BF generators
    std::string bf_movePtr(int const addr);
    std::string bf_setToValue(int const addr, int const val);
    std::string bf_setToValue(int const start, int const val, size_t const n);
    std::string bf_assign(int const lhs, int const rhs);
    std::string bf_assign(int const dest, int const src, size_t const n);
    std::string bf_addTo(int const target, int const rhs);
    std::string bf_incr(int const target);
    std::string bf_decr(int const target);
    std::string bf_subtractFrom(int const target, int const rhs);
    std::string bf_multiply(int const lhs, int const rhs, int const result);
    std::string bf_multiplyBy(int const target, int const rhs);
    std::string bf_equal(int const lhs, int const rhs, int const result);
    std::string bf_notEqual(int const lhs, int const rhs, int const result);
    std::string bf_greater(int const lhs, int const rhs, int const result);
    std::string bf_less(int const lhs, int const rhs, int const result);
    std::string bf_greaterOrEqual(int const lhs, int const rhs, int const result);
    std::string bf_lessOrEqual(int const lhs, int const rhs, int const result);
    std::string bf_not(int const operand, int const result);
    std::string bf_and(int const lhs, int const rhs, int const result);
    std::string bf_or(int const lhs, int const rhs, int const result);
    
    // Instruction generator
    template <auto Member, typename ... Args>
    Instruction instruction(Args ... args){
        std::string file = d_parser.filename();
        int line = d_parser.lineNr();
        return Instruction([=, this](){
                               setFilename(file);
                               setLineNr(line);
                               return (this->*Member)(args ...);
                           });
    }
    
    // Wrappers for element-modifying instructions
    using UnaryFunction = int (Compiler::*)(AddressOrInstruction const &);
    int applyUnaryFunctionToElement(std::string const &arrayIdent,
                                    AddressOrInstruction const &index,
                                    UnaryFunction func);
    
    using BinaryFunction = int (Compiler::*)(AddressOrInstruction const &, AddressOrInstruction const &);
    int applyBinaryFunctionToElement(std::string const &arrayIdent,
                                     AddressOrInstruction const &index,
                                     AddressOrInstruction const &rhs,
                                     BinaryFunction func);

    // Instructions
    int inlineBF(std::string const &str);
    int sizeOfOperator(std::string const &ident);
    int movePtr(std::string const &ident);
    int constVal(uint8_t const val);
    int statement(Instruction const &instr);
    int mergeInstructions(Instruction const &instr1, Instruction const &instr2);
    int arrayFromSizeStaticValue(int const sz, uint8_t const val = 0);
    int arrayFromSizeDynamicValue(int const sz, AddressOrInstruction const &val);
    int arrayFromList(std::vector<Instruction> const &list);
    int arrayFromString(std::string const &str);
    int call(std::string const &functionName, std::vector<Instruction> const &args = {});
    int declareVariable(std::string const &ident, int const sz);
    int initializeExpression(std::string const &ident, int const sz, Instruction const &rhs);
    int assign(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int fetch(std::string const &ident);
    int assignElement(std::string const &ident, AddressOrInstruction const &index, AddressOrInstruction const &rhs);
    int fetchElement(std::string const &ident, AddressOrInstruction const &index);
    int preIncrement(AddressOrInstruction const &addr);
    int preDecrement(AddressOrInstruction const &addr);
    int postIncrement(AddressOrInstruction const &addr);
    int postDecrement(AddressOrInstruction const &addr);
    int addTo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int add(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int subtractFrom(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int subtract(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int multiplyBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int multiply(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int equal(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int notEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int less(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int greater(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int lessOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int greaterOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int logicalNot(AddressOrInstruction const &arg);
    int logicalAnd(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int logicalOr(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int divideBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int divide(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int moduloBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int modulo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int divMod(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int modDiv(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    std::pair<int, int> divModPair(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);

    int ifStatement(Instruction const &condition, Instruction const &ifBody, Instruction const &elseBody);  
    int forStatement(Instruction const &init, Instruction const &condition,
                     Instruction const &increment, Instruction const &body);    
    int whileStatement(Instruction const &condition, Instruction const &body);

    // Error handling (implementations below)
    template <typename First, typename ... Rest>
    void errorIf(bool const condition, First const &first, Rest&& ... rest) const;

    template <typename ... Rest>
    void validateAddr__(std::string const &function, int first, Rest&& ... rest) const;

    void setFilename(std::string const &file);
    void setLineNr(int const line);
    int lineNr() const;
    std::string filename() const;
};


// Implementation of the errorIf<> and validateAddr__<> function templates

template <typename First, typename ... Rest>
void Compiler::errorIf(bool const condition, First const &first, Rest&& ... rest) const
{
    if (!condition) return;

    std::cerr << "Error in " << filename() << " on line " << lineNr() << ": " << first;
    (std::cerr << ... << rest) << '\n';

    try {
        d_parser.ERROR();
    }
    catch (...)
    {
        std::cerr << "Unable to recover: compilation terminated.\n";
        std::exit(1);
    }
}

template <typename ... Rest>
void Compiler::validateAddr__(std::string const &function, int first, Rest&& ... rest) const
{
    if (first < 0 || ((rest < 0) || ...))
    {
        std::cerr << "Fatal internal error while compiling " << filename()
                  << ", line " << lineNr()
                  << ": negative address passed to " << function << "()\n\n"
                  << "Compilation terminated\n";
        std::exit(1);
    }

    int const sz = (int)d_memory.size();
    if (first > sz || (((int)rest >= sz) || ...))
    {
        std::cerr << "Fatal internal error while compiling " << filename()
                  << ", line " << lineNr()
                  << ": address out of bounds passed to " << function << "()\n\n"
                  << "Compilation terminated.\n";

        std::exit(1);
    }
}



#endif //COMPILER_H
