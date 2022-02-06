#ifndef COMPILER_H
#define COMPILER_H

#include "compilerbase.h"
#undef Compiler
// CAVEAT: between the baseclass-include directive and the 
// #undef directive in the previous line references to Compiler 
// are read as CompilerBase.
// If you need to include additional headers in this file 
// you should do so after these comment-lines.

#include <string>
#include <map>
#include <deque>
#include <stack>
#include <sstream>
#include "scanner.h"
#include "bfgenerator.h"
#include "memory.h"

class Compiler: public CompilerBase
{
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
    
    static constexpr int TAPE_SIZE_INITIAL = 30000;
    long const MAX_INT;
    long const MAX_ARRAY_SIZE;

    Scanner     d_scanner;
    Memory      d_memory;
    BFGenerator d_bfGen;

    std::map<std::string, BFXFunction>  d_functionMap;
    std::map<std::string, int>          d_constMap;
    std::ostringstream                  d_codeBuffer;
    std::stack<int>                     d_stack;
    ScopeStack                          d_scopeStack;
    
    enum class Stage
        {
         IDLE,
         PARSING,
         CODEGEN,
         FINISHED
        };

    Stage       d_stage{Stage::IDLE};
    std::string d_instructionFilename;
    int         d_instructionLineNr;
    
public:
    enum class CellType
        {
         INT8,
         INT16,
         INT32
        };

    Compiler(std::string const &file, CellType type);
    int compile();
    void write(std::ostream &out);

private:
    int parse();
    void addFunction(BFXFunction const &bfxFunc);
    void addGlobals(std::vector<std::pair<std::string, int>> const &declarations);
    void addConstant(std::string const &ident, int const num);

    int  compileTimeConstant(std::string const &ident) const;
    bool isCompileTimeConstant(std::string const &ident) const;

    static bool validateInlineBF(std::string const &ident);
    static std::string cancelOppositeCommands(std::string const &bf);
    
    // Memory management uitilities
    int  allocate(std::string const &ident, int const sz = 1);
    int  allocateTemp(int const sz = 1);
    int  allocateTempBlock(int const sz);
    int  addressOf(std::string const &ident);
    void pushStack(int const addr);
    int  popStack();

        // Instruction generator
    template <auto Member, typename ... Args>
    Instruction instruction(Args ... args){
        std::string file = d_scanner.filename();
        int line = d_scanner.lineNr();
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
    int constVal(int const val);
    int statement(Instruction const &instr);
    int mergeInstructions(Instruction const &instr1, Instruction const &instr2);
    int arrayFromSizeStaticValue(int const sz, int const val = 0);
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
    int switchStatement(Instruction const &compareExpr,
                        std::vector<std::pair<Instruction, Instruction>> const &cases,
                        Instruction const &defaultCase);

    // Error handling (implementations below)
    template <typename First, typename ... Rest>
    void compilerError(First const &first, Rest&& ... rest) const;

    template <typename First, typename ... Rest>
    void compilerWarning(First const &first, Rest&& ... rest) const;

    template <typename First, typename ... Rest>
    void errorIf(bool const condition, First const &first, Rest&& ... rest) const;

    template <typename First, typename ... Rest>
    void warningIf(bool const condition, First const &first, Rest&& ... rest) const;

    template <typename ... Rest>
    void validateAddr__(std::string const &function, int first, Rest&& ... rest) const;

    void setFilename(std::string const &file);
    void setLineNr(int const line);
    int lineNr() const;
    std::string filename() const;


    // BisonC++ generated
    void error();                   // called on (syntax) errors
    int lex();
    void print();

    void exceptionHandler(std::exception const &exc);

    // support functions for parse():
    void executeAction_(int ruleNr);
    void errorRecovery_();
    void nextCycle_();
    void nextToken_();
    void print_();
};


// Implementation of the errorIf<> and validateAddr__<> function templates
template <typename First, typename ... Rest>
void Compiler::compilerWarning(First const &first, Rest&& ... rest) const
{
    std::cerr << "Warning: in " << filename() << " on line " << lineNr() << ": " << first;
    (std::cerr << ... << rest) << '\n';
}

template <typename First, typename ... Rest>
void Compiler::compilerError(First const &first, Rest&& ... rest) const
{
    std::cerr << "Error in " << filename() << " on line " << lineNr() << ": " << first;
    (std::cerr << ... << rest) << '\n';

    try {
        CompilerBase::ERROR();
    }
    catch (...)
    {
        std::cerr << "Unable to recover: compilation terminated.\n";
        std::exit(1);
    }
}

template <typename First, typename ... Rest>
void Compiler::errorIf(bool const condition, First const &first, Rest&& ... rest) const
{
    if (condition)
        compilerError(first, std::forward<Rest>(rest)...);
}

template <typename First, typename ... Rest>
void Compiler::warningIf(bool const condition, First const &first, Rest&& ... rest) const
{
    if (condition)
        compilerWarning(first, std::forward<Rest>(rest)...);
}


#endif //COMPILER_H
