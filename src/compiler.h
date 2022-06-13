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
#include <sstream>
#include "scanner.h"
#include "bfgenerator.h"
#include "memory.h"
#include "scope.h"

class Compiler: public CompilerBase
{
public:
    enum class CellType
        {
         INT8,
         INT16,
         INT32
        };

    struct Options
    {
        Compiler::CellType        cellType{Compiler::CellType::INT8};
        std::vector<std::string>  includePaths;
        std::string               bfxFile;
        std::ostream*             outStream{&std::cout};
        bool                      constEvalEnabled{true};
        bool                      randomEnabled{false};
        bool                      bcrEnabled{true};
        bool                      includeWarningEnabled{true};
        int                       maxUnrollIterations{20};
    };

private:
    static constexpr int TAPE_SIZE_INITIAL{30000};

    long const MAX_INT;
    long const MAX_ARRAY_SIZE;
    int  const MAX_LOOP_UNROLL_ITERATIONS{20};
    
    Scanner     d_scanner;
    Memory      d_memory;
    Scope       d_scope;
    BFGenerator d_bfGen;

    std::map<std::string, BFXFunction>         d_functionMap;
    std::map<std::string, int>                 d_constMap;
    std::vector<std::string>                   d_includePaths;
    std::vector<std::string>                   d_included;
    std::ostringstream                         d_codeBuffer;

    using BcrMapType = std::map<std::string, std::pair<int, int>>;
    BcrMapType d_bcrMap;
    
    enum class Stage
        {
         IDLE,
         PARSING,
         CODEGEN,
         FINISHED
        };

    Stage         d_stage{Stage::IDLE};
    std::string   d_instructionFilename;
    int           d_instructionLineNr;
    bool          d_constEvalEnabled{true};
    bool const    d_randomExtensionEnabled{false};
    bool          d_returnExistingAddressOnAlloc{false};
    bool          d_boundsCheckingEnabled{true};
    bool const    d_bcrEnabled{true};
    bool const    d_includeWarningEnabled{true};
    std::ostream& d_outStream;

    struct State
    {
        Memory memory;
        Scope  scope;
        BFGenerator bfGen;
        std::string buffer;
        bool constEval;
        bool returnExisting;
        bool boundsChecking;
        BcrMapType bcrMap;
    };

    enum class SubScopeType
        {
         FOR,
         WHILE,
         SWITCH,
         IF,
         ANONYMOUS
        };
    
public:

    Compiler(Options const &opt);
    int compile();
    void write();

private:
    int parse();
    void pushStream(std::string const &file);
    std::string fileWithoutPath(std::string const &file);
    void addFunction(BFXFunction const &bfxFunc);
    void addGlobals(std::vector<std::pair<std::string, TypeSystem::Type>> const &declarations);
    void addConstant(std::string const &ident, int const num);
    void addStruct(std::string const &name,
                   std::vector<std::pair<std::string, TypeSystem::Type>> const &fields);

    int  compileTimeConstant(std::string const &ident) const;
    bool isCompileTimeConstant(std::string const &ident) const;

    State save();
    void restore(State &&state);
    void enterScope(Scope::Type const type);
    void enterScope(std::string const &name);
    void exitScope(std::string const &name = "");
    void allocateBCRFlags(bool const alloc);
    int getCurrentContinueFlag() const;
    int getCurrentBreakFlag() const;
    void resetContinueFlag();
    

    bool setConstEval(bool const val);
    bool enableConstEval();
    bool disableConstEval();
    void disableBoundChecking();
    void enableBoundChecking();
    void sync(int const addr);
    int wrapValue(int val);
    void constEvalSetToValue(int const addr, int const val);
    void runtimeSetToValue(int const addr, int const val);
    void runtimeAssign(int const lhs, int const rhs);
    
    static bool validateFunction(BFXFunction const &bfxFunc);
    static std::string cancelOppositeCommands(std::string const &bf);
    
    // Memory management uitilities
    int allocate(std::string const &ident, TypeSystem::Type type);
    int allocateTemp(TypeSystem::Type type);
    int allocateTemp(int const sz = 1);
    int allocateTempBlock(int const sz);
    int addressOf(std::string const &ident);

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
    int applyUnaryFunctionToElement(AddressOrInstruction const &arr,
                                    AddressOrInstruction const &index,
                                    UnaryFunction func);
    
    using BinaryFunction = int (Compiler::*)(AddressOrInstruction const &, AddressOrInstruction const &);
    int applyBinaryFunctionToElement(AddressOrInstruction const &arr,
                                     AddressOrInstruction const &index,
                                     AddressOrInstruction const &rhs,
                                     BinaryFunction func);

    // Instructions
    int sizeOfOperator(std::string const &ident);
    int constVal(int const val);
    int statement(Instruction const &instr);
    int mergeInstructions(Instruction const &instr1, Instruction const &instr2);
    int arrayFromSize(int const sz, Instruction const &fill);
    int arrayFromList(std::vector<Instruction> const &list);
    int arrayFromString(std::string const &str);
    int anonymousStructObject(std::string const name, std::vector<Instruction> const &values);
    int call(std::string const &functionName, std::vector<Instruction> const &args = {});
    int declareVariable(std::string const &ident, TypeSystem::Type type);
    int initializeExpression(std::string const &ident, TypeSystem::Type type,
                             Instruction const &rhs);
    int assign(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int fetch(std::string const &ident);
    int fetchField(std::vector<std::string> const &expr);
    int fetchFieldImpl(std::vector<std::string> const &expr, int const baseAddr, size_t const baseIdx);
    
    int fetchElement(AddressOrInstruction const &arr, AddressOrInstruction const &index);
    int assignElement(AddressOrInstruction const &arr, AddressOrInstruction const &index, AddressOrInstruction const &rhs);
    int scanCell();
    int randomCell();
    int printCell(AddressOrInstruction const &target);
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
    int power(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
    int powerBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
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
    void divModPair(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs,
               int const divResult, int const modResult);

    int ifStatement(Instruction const &condition, Instruction const &ifBody, Instruction const &elseBody, bool const scoped = true);  
    int forStatement(Instruction const &init, Instruction const &condition,
                     Instruction const &increment, Instruction const &body);
    int forStatementRuntime(Instruction const &init, Instruction const &condition,
                            Instruction const &increment, Instruction const &body);
    int forRangeStatement(BFXFunction::Parameter const &param, Instruction const &array, Instruction const &body);
    int forRangeStatementRuntime(BFXFunction::Parameter const &param, Instruction const &array, Instruction const &body);
    
    int whileStatement(Instruction const &condition, Instruction const &body);
    int whileStatementRuntime(Instruction const &condition, Instruction const &body);
    
    int switchStatement(Instruction const &compareExpr,
                        std::vector<std::pair<Instruction, Instruction>> const &cases,
                        Instruction const &defaultCase);
    int breakStatement();
    int continueStatement();
    int returnStatement();


    // Constant evaluation
    template <int VolatileMask, typename BFGenFunc, typename ConstFunc, typename ... Args>
    int eval(BFGenFunc&& bfFunc, ConstFunc&& constFunc, int const resultAddr, Args&& ... args);
    
    // Error handling (implementations below)
    template <typename First, typename ... Rest>
    void compilerError(First const &first, Rest&& ... rest) const;

    template <typename First, typename ... Rest>
    void compilerWarning(First const &first, Rest&& ... rest) const;

    template <typename First, typename ... Rest>
    void compilerErrorIf(bool const condition, First const &first, Rest&& ... rest) const;

    template <typename First, typename ... Rest>
    void compilerWarningIf(bool const condition, First const &first, Rest&& ... rest) const;

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
void Compiler::compilerErrorIf(bool const condition, First const &first, Rest&& ... rest) const
{
    if (condition)
        compilerError(first, std::forward<Rest>(rest)...);
}

template <typename First, typename ... Rest>
void Compiler::compilerWarningIf(bool const condition, First const &first, Rest&& ... rest) const
{
    if (condition)
        compilerWarning(first, std::forward<Rest>(rest)...);
}

template <int VolatileMask, typename BFGenFunc, typename ConstFunc, typename ... Args>
int Compiler::eval(BFGenFunc&& bfFunc, ConstFunc&& constFunc, int const resultAddr, Args&& ... args)
{
    static_assert((std::is_convertible_v<int, Args> && ...),
                  "Operands to constEval must be (convertible) to int (addresses).");

    static constexpr int N = (sizeof ... (Args));
    int const arguments[N] = {args ...};
    constexpr auto isVolatile = [](int const argIdx)
                                {
                                    return VolatileMask & (1 << (N - argIdx - 1));
                                };
    

    bool const canBeConstEvaluated = (not d_memory.valueUnknown(args) && ...);
    if (canBeConstEvaluated && d_constEvalEnabled)
    {
        // Evaluate using constfunc
        constEvalSetToValue(resultAddr, constFunc(d_memory.value(args) ...));
        
        // Application of constFunc may have resulted in side-effects if it accepted
        // reference-parameters. Check Mask for volatile values ->
        // for each changed value, set its sync-flag to false.
        
        for (int i = 0; i != N; ++i)
            if (isVolatile(i))
                d_memory.setSync(arguments[i], false);
    }
    else 
    {
        if (d_constEvalEnabled)
        {
            for (int i = 0; i != N; ++i)
                sync(arguments[i]);
        }
        
        bfFunc();

        // Runtime evaluation has resulted in now unknown values. The mask indicates which
        // addresses may have changed during runtime. The return-address will always be
        // set as unknown.
        d_memory.setValueUnknown(resultAddr);
        for (int i = 0; i != N; ++i)
            if (isVolatile(i))
                d_memory.setValueUnknown(arguments[i]);
    }

    return resultAddr;
}



#endif //COMPILER_H
