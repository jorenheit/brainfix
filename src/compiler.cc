#include "compiler.ih"

namespace _MaxInt
{
    template <typename T>
    constexpr size_t _getMax()
    {
        return (static_cast<size_t>(1) << (8 * sizeof(T))) - 1;
    }
    
    static size_t get(Compiler::CellType c)
    {
        switch (c)
        {
        case Compiler::CellType::INT8:  return _getMax<uint8_t>();
        case Compiler::CellType::INT16: return _getMax<uint16_t>();
        case Compiler::CellType::INT32: return _getMax<uint32_t>();;
        }
        throw -1;
    }
}

Compiler::Compiler(std::string const &file, CellType type):
    MAX_INT(_MaxInt::get(type)),
    MAX_ARRAY_SIZE(MAX_INT - 5),
    d_scanner(file, "scannerlog.txt"),
    d_memory(TAPE_SIZE_INITIAL)
{
    d_bfGen.setTempRequestFn([this](){
                                 return allocateTemp();
                             });
    
    d_bfGen.setTempBlockRequestFn([this](int const sz){
                                      return allocateTempBlock(sz);
                                  });
    
    d_bfGen.setMemSizeRequestFn([this](){
                                    return d_memory.size();
                                });
}

int Compiler::lex()
{
    int token = d_scanner.lex();

    switch (token)
    {
    case IDENT:
    case STR:
        {
            d_val_.assign<Tag_::STRING>(d_scanner.matched());
            break;
        }
    case NUM:
        {
            d_val_.assign<Tag_::INT>(std::stoi(d_scanner.matched()));
            break;
        }
    case CHR:
        {
            d_val_.assign<Tag_::CHAR>(d_scanner.matched()[0]);
            break;
        }
    default:
        break;
    }

    return token;
}


int Compiler::compile()
{
    assert(d_stage == Stage::IDLE && "Calling Compiler::compile() multiple times");
    
    d_stage = Stage::PARSING;
    int err = parse();
    if (err)
    {
        std::cerr << "Compilation terminated due to error(s)\n";
        return err;
    }
    
    errorIf(d_functionMap.find("main") == d_functionMap.end(),
            "No entrypoint provided. The entrypoint should be main().");

    d_stage = Stage::CODEGEN;
    call("main");
    d_stage = Stage::FINISHED;
    return 0;
}

void Compiler::write(std::ostream &out)
{
    out << cancelOppositeCommands(d_codeBuffer.str()) << '\n';
}

void Compiler::error()
{
    std::cerr << "ERROR: Syntax error on line "
              << d_scanner.lineNr() << " of file "
              << d_scanner.filename() << '\n';
}

bool Compiler::ScopeStack::empty() const
{
    return d_stack.empty();
}

std::string Compiler::ScopeStack::currentFunction() const
{
    return empty() ? "" : d_stack.back().first;
}

std::string Compiler::ScopeStack::currentScopeString() const
{
    std::string result = currentFunction();
    if (d_stack.size() == 0)
        return result;
    
    for (int scopeId: d_stack.back().second)
        result += std::string("::") + std::to_string(scopeId);

    return result;
}

bool Compiler::ScopeStack::containsFunction(std::string const &name) const
{
    auto const it = std::find_if(d_stack.begin(), d_stack.end(),
                                 [&](std::pair<std::string, StackType<int>> const &item) 
                                 {   
                                     return item.first == name;
                                 });

    return it != d_stack.end();
}

void Compiler::ScopeStack::pushFunctionScope(std::string const &name)
{
    d_stack.push_back(std::pair<std::string, StackType<int>>(name, StackType<int>{}));
}

std::string Compiler::ScopeStack::popFunctionScope()
{
    std::string top = currentScopeString();
    d_stack.pop_back();
    return top;
}

void Compiler::ScopeStack::pushSubScope()
{
    static int counter = 0;
    
    auto &subScopeStack = d_stack.back().second;
    subScopeStack.push_back(++counter);
}

std::string Compiler::ScopeStack::popSubScope()
{
    auto &subScopeStack = d_stack.back().second;
    std::string top = currentScopeString();
    subScopeStack.pop_back();
    return top;
}

void Compiler::addFunction(BFXFunction const &bfxFunc)
{
    auto result = d_functionMap.insert({bfxFunc.name(), bfxFunc});
    errorIf(!result.second,
            "Redefinition of function \"", bfxFunc.name(), "\" is not allowed.");
}

void Compiler::addGlobals(std::vector<std::pair<std::string, int>> const &declarations)
{
    for (auto const &var: declarations)
    {
        std::string const &ident = var.first;
        int const sz = var.second;
        errorIf(sz <= 0, "Global declaration of \"", ident, "\" has invalid size specification.");
        d_memory.allocateGlobal(ident, sz);
    }
}

void Compiler::addConstant(std::string const &ident, int const num)
{
    warningIf(num > MAX_INT, "use of value ", num, " exceeds limit of ", MAX_INT, ".");
    auto result = d_constMap.insert({ident, num});
    errorIf(!result.second,
            "Redefinition of constant ", ident, " is not allowed.");
}

int Compiler::compileTimeConstant(std::string const &ident) const
{
    errorIf(!isCompileTimeConstant(ident),
            ident, " is being used as a const but was not defined as such.");
    
    return d_constMap.at(ident);
}

bool Compiler::isCompileTimeConstant(std::string const &ident) const
{
    return d_constMap.find(ident) != d_constMap.end();
}

int Compiler::allocate(std::string const &ident, int const sz)
{
    int const addr = d_scopeStack.empty() ?
        d_memory.allocateGlobal(ident, sz) :
        d_memory.allocateLocal(ident, d_scopeStack.currentScopeString(), sz);
    
    errorIf(addr < 0, "Variable ", ident, ": variable previously declared.");
    return addr;
}

int Compiler::addressOf(std::string const &ident)
{
    int const addr = d_memory.findLocal(ident, d_scopeStack.currentScopeString());
    return (addr != -1) ? addr : d_memory.findGlobal(ident);
}

int Compiler::allocateTemp(int const sz)
{
    return d_memory.getTemp(d_scopeStack.currentFunction(), sz);
}

int Compiler::allocateTempBlock(int const sz)
{
    return d_memory.getTempBlock(d_scopeStack.currentFunction(), sz);
}

void Compiler::pushStack(int const addr)
{
    d_memory.backup(addr);
    d_stack.push(addr);
}

int Compiler::popStack()
{
    int const addr = d_stack.top();
    d_memory.restore(addr);
    d_stack.pop();
    return addr;
}


int Compiler::inlineBF(std::string const &code)
{
    errorIf(!validateInlineBF(code),
            "Inline BF may not have a net-effect on pointer-position. "
            "Make sure left and right shifts cancel out within each set of [].");
    
    d_codeBuffer << code;
    return -1;
}

bool Compiler::validateInlineBF(std::string const &code)
{
    std::stack<int> countStack;
    int current = 0;
    
    for (char c: code)
    {
        switch (c)
        {
        case '>': ++current; break;
        case '<': --current; break;
        case '[':
            {
                countStack.push(current);
                current = 0;
                break;
            }
        case ']':
            {
                if (current != 0) return false;
                current = countStack.top();
                countStack.pop();
                break;
            }
        default: break;
        }
    }

    return (current == 0);
}

int Compiler::sizeOfOperator(std::string const &ident)
{
    int const addr = addressOf(ident);
    errorIf(addr < 0, "Variable \"", ident ,"\" not defined in this scope.");

    int const tmp = allocateTemp();
    int const sz = d_memory.sizeOf(addr);
    d_codeBuffer << d_bfGen.setToValue(tmp, sz);
    return tmp;
}

int Compiler::movePtr(std::string const &ident)
{
    int const addr = addressOf(ident);
    errorIf(addr < 0, "Variable \"", ident ,"\" not defined in this scope.");

    d_codeBuffer << d_bfGen.movePtr(addr);
    return -1;
}

int Compiler::statement(Instruction const &instr)
{
    instr();
    d_memory.freeTemps(d_scopeStack.currentFunction());
    return -1;
}

int Compiler::call(std::string const &name, std::vector<Instruction> const &args)
{
    // Check if the function exists
    bool const isFunction = d_functionMap.find(name) != d_functionMap.end();
    errorIf(!isFunction,"Call to unknown function \"", name, "\"");
    errorIf(d_scopeStack.containsFunction(name),
            "Function \"", name, "\" is called recursively. Recursion is not allowed.");
    
    // Get the list of parameters
    BFXFunction &func = d_functionMap.at(name);
    auto const &params = func.params();
    errorIf(params.size() != args.size(),
            "Calling function \"", func.name(), "\" with invalid number of arguments. "
            "Expected ", params.size(), ", got ", args.size(), ".");

    std::vector<int> references;
    for (size_t idx = 0; idx != args.size(); ++idx)
    {
        // Evaluate argument that's passed in and get its size
        int const argAddr = args[idx]();
        errorIf(argAddr < 0,
                "Invalid argument argument to function \"", func.name(),
                "\": the expression passed as argument ", idx, " returns void.");

        // Check if the parameter is passed by value or reference
        BFXFunction::Parameter const &p = params[idx];
        std::string const &paramIdent = p.first;
        BFXFunction::ParameterType paramType = p.second;
        if (paramType == BFXFunction::ParameterType::Value)
        {
            // Allocate local variable for the function of the correct size
            // and copy argument to this location
            int const sz = d_memory.sizeOf(argAddr);
            int paramAddr = d_memory.allocateLocal(paramIdent, func.name(), sz);
            errorIf(paramAddr < 0,
                    "Could not allocate parameter", paramIdent, ". ",
                    "Possibly due to duplicate parameter-names.");
            
            assign(paramAddr, argAddr);
        }
        else // Reference
        {
            references.push_back(argAddr);
            pushStack(argAddr);
            d_memory.rename(argAddr, paramIdent, func.name());
        }
    }
    
    // Execute body of the function
    d_scopeStack.pushFunctionScope(func.name());
    func.body()();
    d_scopeStack.popFunctionScope();
    

    // Move return variable to local scope before cleaning up (if non-void)
    int ret = -1;
    if (!func.isVoid())
    {
        // Locate the address of the return-variable
        std::string retVar = func.returnVariable();
        ret = d_memory.findLocal(retVar, func.name());
        errorIf(ret == -1,
                "Returnvalue \"", retVar, "\" of function \"", func.name(),
                "\" seems not to have been declared in the main scope of the function-body.");

        // Check if the return variable was passed into the function as a reference
        bool returnVariableIsReferenceParameter = false;
        for (int const addr: references)
            if (addr == ret)
                returnVariableIsReferenceParameter = true;

        if (returnVariableIsReferenceParameter)
        {
            // Return a copy
            int const tmp = allocateTemp();
            assign(tmp, ret);
            ret = tmp;
        }
        else
        {
            // Pull the variable into local (sub)scope as a temp
            d_memory.rename(ret, "", d_scopeStack.currentScopeString());
            d_memory.markAsTemp(ret);
        }
    }

    // Pull referenced arguments back into original scope
    for (size_t i = 0; i != references.size(); ++i)
        popStack();
    
    // Clean up and return
    d_memory.freeLocals(func.name());
    return ret;
}

int Compiler::constVal(int const num)
{
    warningIf(num > MAX_INT, "use of value ", num, " exceeds limit of ", MAX_INT, ".");
    
    int const tmp = allocateTemp();
    d_codeBuffer << d_bfGen.setToValue(tmp, num);
    return tmp;
}

int Compiler::declareVariable(std::string const &ident, int const sz)
{
    errorIf(sz == 0, "Cannot declare variable \"", ident, "\" of size 0.");
    errorIf(sz < 0,
            "Size must be specified in declaration without initialization of variable ", ident);
    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", (int)sz, ").");

    return allocate(ident, sz);
}

int Compiler::initializeExpression(std::string const &ident, int const sz, Instruction const &rhs)
{
    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", (int)sz, ").");

    errorIf(d_memory.findLocal(ident, d_scopeStack.currentScopeString()) != -1,
            "Variable ", ident, " previously declared.");
    
    int rhsAddr = rhs();
    int const rhsSize = d_memory.sizeOf(rhsAddr);
    int const lhsSize = (sz == -1) ? rhsSize : sz;
    if (d_memory.isTemp(rhsAddr) && rhsSize == lhsSize)
    {
        d_memory.rename(rhsAddr, ident, d_scopeStack.currentScopeString());
        return rhsAddr;
    }
    else
    {
        return assign(allocate(ident, lhsSize), rhsAddr);
    }
}


int Compiler::assign(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void expression in assignment.");
    
    int const leftSize = d_memory.sizeOf(lhs);
    int const rightSize = d_memory.sizeOf(rhs);

    if (leftSize > 1 && rightSize == 1)
    {
        // Fill array with value
        for (int i = 0; i != leftSize; ++i)
            d_codeBuffer << d_bfGen.assign(lhs + i, rhs);
    }
    else if (leftSize == rightSize)
    {
        // Same size -> copy
        d_codeBuffer << d_bfGen.assign(lhs, rhs, leftSize);
    }
    else
    {
        errorIf(true, "Cannot assign variables of unequal sizes (sizeof(lhs) = ", leftSize,
                " vs sizeof(rhs) = ", rightSize, ").");
    }

    return lhs;
}

int Compiler::fetch(std::string const &ident)
{
    if (isCompileTimeConstant(ident))
        return constVal(compileTimeConstant(ident));
    
    int const addr = addressOf(ident);
    errorIf(addr < 0, "Variable \"", ident, "\" undefined in this scope.");
    return addr;
}

int Compiler::arrayFromSizeStaticValue(int const sz, int const val)
{
    warningIf(val > MAX_INT, "use of value ", val, " exceeds limit of ", MAX_INT, ".");
    
    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");
    
    int const start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
        d_codeBuffer << d_bfGen.setToValue(start + idx, val);

    return start;
}


int Compiler::arrayFromSizeDynamicValue(int const sz, AddressOrInstruction const &val)
{
    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    errorIf(d_memory.sizeOf(val) > 1,
            "Array fill-value must refer to a variable of size 1, but it is of size ",
            d_memory.sizeOf(val), ".");
    
    return assign(allocateTemp(sz), val);
}

int Compiler::arrayFromList(std::vector<Instruction> const &list)
{
    int const sz = list.size();

    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");
    
    int start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
        d_codeBuffer << d_bfGen.assign(start + idx, list[idx]());

    return start;
}

int Compiler::arrayFromString(std::string const &str)
{
    int const sz = str.size();

    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    int const start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
        d_codeBuffer << d_bfGen.setToValue(start + idx, str[idx]);

    return start;
}

int Compiler::fetchElement(AddressOrInstruction const &arr, AddressOrInstruction const &index)
{
    int const sz = d_memory.sizeOf(arr);
    int const ret = allocateTemp();

    d_codeBuffer << d_bfGen.fetchElement(arr, sz, index, ret);
    return ret;
}

int Compiler::assignElement(AddressOrInstruction const &arr, AddressOrInstruction const &index, AddressOrInstruction const &rhs)
{
    int const sz = d_memory.sizeOf(arr);
    d_codeBuffer << d_bfGen.assignElement(arr, sz, index, rhs);
    
    // Attention: can't return the address of the modified cell, so we return the
    // address of the known RHS-cell.
    return rhs;
}


int Compiler::applyUnaryFunctionToElement(AddressOrInstruction const &arr,
                                          AddressOrInstruction const &index,
                                          UnaryFunction func)
{
    int const copyOfElement = fetchElement(arr, index);
    int const returnVal = (this->*func)(copyOfElement);
    assignElement(arr, index, copyOfElement);
    return returnVal;
}

int Compiler::applyBinaryFunctionToElement(AddressOrInstruction const &arr,
                                           AddressOrInstruction const &index,
                                           AddressOrInstruction const &rhs,
                                           BinaryFunction func)
{
    int const fetchedAddr = fetchElement(arr, index);
    int const returnAddr  = (this->*func)(fetchedAddr, rhs);
    assignElement(arr, index, fetchedAddr);
    return returnAddr;
}


int Compiler::preIncrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot increment void-expression.");

    d_codeBuffer << d_bfGen.incr(target);
    return target;
}

int Compiler::preDecrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot decrement void-expression.");
    
    d_codeBuffer << d_bfGen.decr(target);
    return target;
}

int Compiler::postIncrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot increment void-expression.");
    
    int const tmp = allocateTemp();
    d_codeBuffer << d_bfGen.assign(tmp, target)
                 << d_bfGen.incr(target);

    return tmp;
}

int Compiler::postDecrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot decrement void-expression.");

    int const tmp = allocateTemp();
    d_codeBuffer << d_bfGen.assign(tmp, target)
                 << d_bfGen.decr(target);

    return tmp;
}

int Compiler::addTo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");

    d_codeBuffer << d_bfGen.addTo(lhs, rhs);
    return lhs;
}

int Compiler::add(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");
    
    int const ret = allocateTemp();
    d_codeBuffer << d_bfGen.assign(ret, lhs)
                 << d_bfGen.addTo(ret, rhs);
    return ret;
}

int Compiler::subtractFrom(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");
    
    d_codeBuffer << d_bfGen.subtractFrom(lhs, rhs);
    return lhs;
}

int Compiler::subtract(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");

    int const ret = allocateTemp();
    d_codeBuffer << d_bfGen.assign(ret, lhs)
                 << d_bfGen.subtractFrom(ret, rhs);
    return ret;
}

int Compiler::multiplyBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");
    
    d_codeBuffer << d_bfGen.multiplyBy(lhs, rhs);
    return lhs;
}

int Compiler::multiply(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");

    int const ret = allocateTemp();
    d_codeBuffer << d_bfGen.multiply(lhs, rhs, ret);
    return ret;
}

int Compiler::divideBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");
    
    return assign(lhs, divide(lhs, rhs));
}

int Compiler::divide(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");
    
    return divModPair(lhs, rhs).first;
}

int Compiler::moduloBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in modulo-operation.");
    
    return assign(lhs, modulo(lhs, rhs));
}

int Compiler::modulo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in modulo-operation.");
    
    return divModPair(lhs, rhs).second;
}

int Compiler::divMod(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in divmod-operation.");
    
    auto const pr = divModPair(lhs, rhs);
    assign(lhs, pr.first);
    return pr.second;
}

int Compiler::modDiv(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in moddiv-operation.");
    
    auto const pr = divModPair(lhs, rhs);
    assign(lhs, pr.second);
    return pr.first;
}

std::pair<int, int> Compiler::divModPair(AddressOrInstruction const &num, AddressOrInstruction const &denom)
{
    int const tmp = allocateTempBlock(2);
    int const divResult = tmp + 0;
    int const modResult = tmp + 1;

    d_codeBuffer << d_bfGen.divmod(num, denom, divResult, modResult);
    return {divResult, modResult};
}


int Compiler::equal(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.equal(lhs, rhs, result);
    return result;
}

int Compiler::notEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.notEqual(lhs, rhs, result);
    return result;
}

int Compiler::less(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.less(lhs, rhs, result);
    return result;
}

int Compiler::greater(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.greater(lhs, rhs, result);
    return result;
}

int Compiler::lessOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.lessOrEqual(lhs, rhs, result);
    return result;
}

int Compiler::greaterOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.greaterOrEqual(lhs, rhs, result);
    return result;
}

int Compiler::logicalNot(AddressOrInstruction const &arg)
{
    errorIf(arg < 0, "Use of void-expression in not-operation.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.logicalNot(arg, result);

    return result;
}

int Compiler::logicalAnd(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in and-operation.");
    
    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.logicalAnd(lhs, rhs, result);
    return result;
}

int Compiler::logicalOr(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in or-operation.");

    int const result = allocateTemp();
    d_codeBuffer << d_bfGen.logicalOr(lhs, rhs, result);
    return result;
}

int Compiler::ifStatement(Instruction const &condition, Instruction const &ifBody, Instruction const &elseBody)
{
    int const conditionAddr = condition();
    errorIf(conditionAddr < 0, "Use of void-expression in if-condition.");

    int const ifFlag = allocateTemp();
    assign(ifFlag, conditionAddr);
    int const elseFlag = logicalNot(ifFlag);

    pushStack(ifFlag);
    pushStack(elseFlag); 

    d_codeBuffer << d_bfGen.movePtr(ifFlag)
                 << "[";

    {
        d_scopeStack.pushSubScope();
        ifBody();
        std::string const outOfScope = d_scopeStack.popSubScope();
        d_memory.freeLocals(outOfScope);
    }
    
    d_codeBuffer << d_bfGen.setToValue(ifFlag, 0)
                 << "]"
                 << d_bfGen.movePtr(elseFlag)
                 << "[";

    {
        d_scopeStack.pushSubScope();
        elseBody();
        std::string const outOfScope = d_scopeStack.popSubScope();
        d_memory.freeLocals(outOfScope);
    }
    
    d_codeBuffer << d_bfGen.setToValue(elseFlag, 0)
                 << "]";

    popStack();
    popStack();
    return -1;
}

int Compiler::mergeInstructions(Instruction const &instr1, Instruction const &instr2)
{
    instr1();
    instr2();

    return -1;
}

int Compiler::forStatement(Instruction const &init, Instruction const &condition,
                           Instruction const &increment, Instruction const &body)
{
    int const flag = allocateTemp();
    pushStack(flag);
    d_scopeStack.pushSubScope();

    init();
    int const conditionAddr = condition();
    errorIf(conditionAddr < 0, "Use of void-expression in for-condition.");
    
    d_codeBuffer << d_bfGen.assign(flag, conditionAddr)
                 << "[";

    body();
    increment();
    d_codeBuffer << d_bfGen.assign(flag, condition())
                 << "]";

    std::string outOfScope = d_scopeStack.popSubScope();
    popStack();
    
    d_memory.freeLocals(outOfScope);
    return -1;
}

int Compiler::whileStatement(Instruction const &condition, Instruction const &body)
{
    int const flag = allocateTemp();
    int const conditionAddr = condition();
    errorIf(conditionAddr < 0, "Use of void-expression in while-condition.");

    d_scopeStack.pushSubScope();
    pushStack(flag);

    d_codeBuffer << d_bfGen.assign(flag, conditionAddr)
                 << "[";
    body();

    d_codeBuffer << d_bfGen.assign(flag, condition())
                 << "]";

    std::string outOfScope = d_scopeStack.popSubScope();
    popStack();

    d_memory.freeLocals(outOfScope);
    return -1;
}

int Compiler::switchStatement(Instruction const &compareExpr,
                              std::vector<std::pair<Instruction, Instruction>> const &cases,
                              Instruction const &defaultCase)
{
    int const compareAddr = compareExpr();
    int const goToDefault = allocateTemp();
    pushStack(compareAddr);
    pushStack(goToDefault);

    d_codeBuffer << d_bfGen.setToValue(goToDefault, 1);
    for (auto const &pr: cases)
    {
        int const caseAddr = pr.first();
        int const flag = allocateTemp();
        pushStack(flag);
        
        d_codeBuffer << d_bfGen.equal(compareAddr, caseAddr, flag)
                     << "["
                     <<     d_bfGen.setToValue(goToDefault, 0);

        d_scopeStack.pushSubScope();
        pr.second();
        std::string const outOfScope = d_scopeStack.popSubScope();
        d_memory.freeLocals(outOfScope);

        d_codeBuffer << d_bfGen.setToValue(flag, 0)
                     << "]";

        popStack();
    }

    d_codeBuffer << d_bfGen.movePtr(goToDefault)
                 << "[";

    d_scopeStack.pushSubScope();
    defaultCase();
    std::string const outOfScope = d_scopeStack.popSubScope();
    d_memory.freeLocals(outOfScope);

    d_codeBuffer << d_bfGen.setToValue(goToDefault, 0)
                 << "]";
    
    popStack(); // goToDefault
    popStack(); // compareAddr
    return -1;
}


std::string Compiler::cancelOppositeCommands(std::string const &bf)
{
    auto cancel = [](std::string const &input, char up, char down) -> std::string
                  {
                      std::string result;
                      int count = 0;

                      auto flush = [&]()
                                   {
                                       if (count > 0) result += std::string( count, up);
                                       if (count < 0) result += std::string(-count, down);
                                       count = 0;
                                   };

    
                      for (char c: input)
                      {
                          if (c == up)   ++count;
                          else if (c == down) --count;
                          else
                          {
                              flush();
                              result += c;
                          }
                      }
    
                      flush();
                      return result;
                  };

    std::string result  = cancel(bf, '>', '<');
    return cancel(result, '+', '-');
}

void Compiler::setFilename(std::string const &file)
{
    d_instructionFilename = file;
}

void Compiler::setLineNr(int const line)
{
    d_instructionLineNr = line;
}

int Compiler::lineNr() const
{
    if (d_stage == Stage::PARSING)
        return d_scanner.lineNr();
    else if (d_stage == Stage::CODEGEN)
        return d_instructionLineNr;

    assert(false && "Unreachable");
    return -1;
}

std::string Compiler::filename() const
{
    if (d_stage == Stage::PARSING)
        return d_scanner.filename();
    else if (d_stage == Stage::CODEGEN)
        return d_instructionFilename;

    assert(false && "Unreachable");
    return "";
}
