#include "compiler.ih"

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

int Compiler::compile()
{
    assert(d_stage == Stage::IDLE && "Calling Compiler::compile() multiple times");
    
    d_stage = Stage::PARSING;
    int err = d_parser.parse();
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

void Compiler::addConstant(std::string const &ident, uint8_t const num)
{
    auto result = d_constMap.insert({ident, num});
    errorIf(!result.second,
            "Redefinition of constant ", ident, " is not allowed.");
}

uint8_t Compiler::compileTimeConstant(std::string const &ident) const
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
    
    errorIf(addr < 0, "Failed to allocate ", ident, ": variable previously declared.");
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
    d_memory.stack(addr);
    d_stack.push(addr);
}

int Compiler::popStack()
{
    int const addr = d_stack.top();
    d_memory.unstack(addr);
    d_stack.pop();
    return addr;
}

std::string Compiler::bf_setToValue(int const addr, int const val)
{
    validateAddr(addr, val);

    std::ostringstream ops;
    ops << bf_movePtr(addr)        // go to address
        << "[-]"                   // reset cell to 0
        << std::string(val, '+');  // increment to value

    return ops.str();
}

std::string Compiler::bf_setToValue(int const start, int const val, size_t const n)
{
    validateAddr(start, val);
    
    std::ostringstream ops;
    for (size_t i = 0; i != n; ++i)
        ops << bf_setToValue(start + i, val);

    return ops.str();
}

std::string Compiler::bf_assign(int const lhs, int const rhs)
{
    validateAddr(lhs, rhs);
    
    int const tmp = allocateTemp();

    std::ostringstream ops;
    ops    << bf_setToValue(lhs, 0)
           << bf_setToValue(tmp, 0)

        // Move contents of RHS to both LHS and TMP (backup)
           << bf_movePtr(rhs)         
           << "["
           <<     bf_incr(lhs)
           <<     bf_incr(tmp)
           <<     bf_decr(rhs)
           << "]"

        // Restore RHS by moving TMP back into it
           << bf_movePtr(tmp)
           << "["
           <<     bf_incr(rhs)
           <<     bf_decr(tmp)
           << "]"

        // Leave pointer at lhs
           << bf_movePtr(lhs);

    return ops.str();
}

std::string Compiler::bf_assign(int const dest, int const src, size_t const n)
{
    validateAddr(dest, src);
    
    std::ostringstream ops;
    for (size_t idx = 0; idx != n; ++idx)
        ops << bf_assign(dest + idx, src + idx);

    return ops.str();
}

std::string Compiler::bf_movePtr(int const addr)
{
    validateAddr(addr);
    
    int const diff = (int)addr - (int)d_pointer;
    d_pointer = addr;
    return (diff >= 0) ? std::string(diff, '>')    : std::string(-diff, '<');
}

std::string Compiler::bf_addTo(int const target, int const rhs)
{
    validateAddr(target, rhs);
    
    std::ostringstream ops;
    int const tmp = allocateTemp();
    ops    << bf_assign(tmp, rhs)
           << "["
           <<     bf_incr(target)
           <<     bf_decr(tmp)
           << "]"
           << bf_movePtr(target);
    
    return ops.str();
}

std::string Compiler::bf_subtractFrom(int const target, int const rhs)
{
    validateAddr(target, rhs);
    
    int const tmp = allocateTemp();
    std::ostringstream ops;
    ops    << bf_assign(tmp, rhs)
           << "["
           <<     bf_decr(target)
           <<     bf_decr(tmp)
           << "]"
           << bf_movePtr(target);
    
    return ops.str();
}

std::string Compiler::bf_incr(int const target)
{
    validateAddr(target);
    return bf_movePtr(target) + "+";
}

std::string Compiler::bf_decr(int const target)
{
    validateAddr(target);
    return bf_movePtr(target) + "-";
}

std::string Compiler::bf_multiply(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    std::ostringstream ops;
    ops << bf_assign(result, lhs)
        << bf_multiplyBy(result, rhs);

    return ops.str();
}

std::string Compiler::bf_multiplyBy(int const target, int const factor)
{
    validateAddr(target, factor);

    int const tmp = allocateTempBlock(2);
    int const targetCopy = tmp + 0;
    int const count      = tmp + 1;

    std::ostringstream ops;
    ops << bf_assign(targetCopy, target)
        << bf_setToValue(target, 0)
        << bf_assign(count, factor)
        << "["
        <<     bf_addTo(target, targetCopy)
        <<     bf_decr(count)
        << "]"
        << bf_movePtr(target);
    
    return ops.str();
}

std::string Compiler::bf_not(int const addr, int const result)
{
    validateAddr(addr, result);
    
    int const tmp = allocateTemp();
    std::ostringstream ops;
    
    ops    << bf_setToValue(result, 1)
           << bf_assign(tmp, addr)
           << "["
           <<     bf_setToValue(result, 0)
           <<     bf_setToValue(tmp, 0)
           << "]"
           << bf_movePtr(result);

    return ops.str();
}

std::string Compiler::bf_and(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp = allocateTempBlock(2);
    int const x = tmp + 0;
    int const y = tmp + 1;
    
    std::ostringstream ops;
    ops    << bf_setToValue(result, 0)
           << bf_assign(y, rhs)
           << bf_assign(x, lhs)
           << "["
           <<     bf_movePtr(y)
           <<     "["
           <<         bf_setToValue(result, 1)
           <<         bf_setToValue(y, 0)
           <<     "]"
           <<     bf_setToValue(x, 0)
           << "]"
           << bf_movePtr(result);

    return ops.str();
}

std::string Compiler::bf_or(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp = allocateTempBlock(2);
    int const x = tmp + 0;
    int const y = tmp + 1;

    std::ostringstream ops;
    ops    << bf_setToValue(result, 0)
           << bf_assign(x, lhs)
           << "["
           <<     bf_setToValue(result, 1)
           <<     bf_setToValue(x, 0)
           << "]"
           << bf_assign(y, rhs)
           << "["
           <<     bf_setToValue(result, 1)
           <<     bf_setToValue(y, 0)
           << "]"
           << bf_movePtr(result);

    return ops.str();
}

std::string Compiler::bf_equal(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp = allocateTempBlock(2);
    int const x = tmp + 0;
    int const y = tmp + 1;

    std::ostringstream ops;
    ops << bf_setToValue(result, 1)
        << bf_assign(y, rhs)
        << bf_assign(x, lhs)
        << "["
        <<     bf_decr(y)
        <<     bf_decr(x)
        << "]"
        << bf_movePtr(y)
        << "["
        <<     bf_setToValue(result, 0)
        <<     bf_setToValue(y, 0)
        << "]"
        << bf_movePtr(result);

    return ops.str();
}

std::string Compiler::bf_notEqual(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);
    
    int const isEqual = allocateTemp();
    std::ostringstream ops;
    ops << bf_equal(lhs, rhs, isEqual)
        << bf_not(isEqual, result);

    return ops.str();
}

std::string Compiler::bf_greater(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp  = allocateTempBlock(4);
    int const x    = tmp + 0;
    int const y    = tmp + 1;
    int const tmp1 = tmp + 2;
    int const tmp2 = tmp + 3;
        
    std::ostringstream ops;
    ops    << bf_setToValue(tmp1, 0)
           << bf_setToValue(tmp2, 0)
           << bf_setToValue(result, 0)
           << bf_assign(y, rhs)
           << bf_assign(x, lhs)
           << "["
           <<     bf_incr(tmp1)
           <<     bf_movePtr(y)
           <<     "["
           <<         bf_setToValue(tmp1, 0)
           <<         bf_incr(tmp2)
           <<         bf_decr(y)
           <<     "]"
           <<     bf_movePtr(tmp1)
           <<     "["
           <<         bf_incr(result)
           <<         bf_decr(tmp1)
           <<     "]"
           <<     bf_movePtr(tmp2)
           <<     "["
           <<         bf_incr(y)
           <<         bf_decr(tmp2)
           <<     "]"
           <<     bf_decr(y)
           <<     bf_decr(x)
           << "]"
           << bf_movePtr(result);
    
    return ops.str();
}

std::string Compiler::bf_less(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);
    
    return bf_greater(rhs, lhs, result); // reverse arguments
}

std::string Compiler::bf_greaterOrEqual(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp       = allocateTempBlock(2);
    int const isEqual   = tmp + 0;
    int const isGreater = tmp + 1;

    std::ostringstream ops;
    ops    << bf_equal(lhs, rhs, isEqual)
           << bf_greater(lhs, rhs, isGreater)
           << bf_or(isEqual, isGreater, result);

    return ops.str();
}

std::string Compiler::bf_lessOrEqual(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);
    
    return bf_greaterOrEqual(rhs, lhs, result); // reverse arguments
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
    d_codeBuffer << bf_setToValue(tmp, sz);
    return tmp;
}

int Compiler::movePtr(std::string const &ident)
{
    int const addr = addressOf(ident);
    errorIf(addr < 0, "Variable \"", ident ,"\" not defined in this scope.");

    d_codeBuffer << bf_movePtr(addr);
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

    std::vector<std::tuple<int, std::string, std::string>> references;
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
            // Change scope of the argument to that of the function
            references.push_back({
                                  argAddr,
                                  d_memory.identifier(argAddr),
                                  d_memory.scope(argAddr)
                });
            
            d_memory.changeScope(argAddr, func.name());
            d_memory.changeIdent(argAddr, paramIdent);
            pushStack(argAddr);
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
        for (auto const &tup: references)
            if (std::get<0>(tup) == ret)
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
            d_memory.changeScope(ret, d_scopeStack.currentScopeString());
            d_memory.markAsTemp(ret);
        }
    }

    // Pull referenced arguments back into original scope
    for (auto const &tup: references)
    {
        int const argAddr = std::get<0>(tup);
        std::string const &originalName = std::get<1>(tup);
        std::string const &originalScope = std::get<2>(tup);
        
        d_memory.changeIdent(argAddr, originalName);
        d_memory.changeScope(argAddr, originalScope);
        popStack();
    }
    
    // Clean up and return
    d_memory.freeLocals(func.name());
    return ret;
}

int Compiler::constVal(uint8_t const num)
{
    int const tmp = allocateTemp();
    d_codeBuffer << bf_setToValue(tmp, num);
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
    int rhsAddr = rhs();
    int const rhsSize = d_memory.sizeOf(rhsAddr);
    int const lhsSize = (sz == -1) ? rhsSize : sz;
    if (d_memory.isTemp(rhsAddr) && rhsSize == lhsSize)
    {
        d_memory.markAsLocal(rhsAddr, ident, d_scopeStack.currentScopeString());
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
            d_codeBuffer << bf_assign(lhs + i, rhs);
    }
    else if (leftSize == rightSize)
    {
        // Same size -> copy
        d_codeBuffer << bf_assign(lhs, rhs, leftSize);
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

int Compiler::arrayFromSizeStaticValue(int const sz, uint8_t const val)
{
    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");
    
    int const start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
        d_codeBuffer << bf_setToValue(start + idx, val);

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
        d_codeBuffer << bf_assign(start + idx, list[idx]());

    return start;
}

int Compiler::arrayFromString(std::string const &str)
{
    int const sz = str.size();

    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    int const start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
        d_codeBuffer << bf_setToValue(start + idx, str[idx]);

    return start;
}

int Compiler::fetchElement(std::string const &ident, AddressOrInstruction const &index)
{
    // Algorithms to move an unknown amount to the left and right.
    // Assumes the pointer points to a cell containing the amount
    // it needs to be shifted and a copy of this amount adjacent to it.
    // Also, neighboring cells must all be zeroed out.
               
    static std::string const dynamicMoveRight = "[>[->+<]<[->+<]>-]";
    static std::string const dynamicMoveLeft  = "[<[-<+>]>[-<+>]<-]<";

    // Allocate a bufferwith 2 additional cells:
    // 1. to keep a copy of the index
    // 2. to store a temporary necessary for copying

    int const arr = addressOf(ident);
    errorIf(arr < 0, "Variable \"", ident, "\" undefined in this scope.");

    int const sz = d_memory.sizeOf(arr);
    int const bufSize = sz + 2;
    int const buf = allocateTemp(bufSize);
    int const ret = allocateTemp();
    int const dist = buf - arr;

    std::string const arr2buf(std::abs(dist), (dist > 0 ? '>' : '<'));
    std::string const buf2arr(std::abs(dist), (dist > 0 ? '<' : '>'));

    d_codeBuffer << bf_assign(buf + 0, index)
                 << bf_assign(buf + 1, buf)
                 << bf_setToValue(buf + 2, 0, bufSize - 2)
                 << bf_movePtr(buf)
                 << dynamicMoveRight
                 << buf2arr
                 << "[-"
                 <<     arr2buf
                 <<     ">>+<<"
                 <<     buf2arr
                 << "]"
                 << arr2buf
                 << ">>"
                 << "["
                 <<     "-<<+"
                 <<     buf2arr
                 <<     "+"
                 <<     arr2buf
                 <<     ">>"
                 << "]"
                 << "<"
                 << dynamicMoveLeft
                 << bf_assign(ret, buf);

    return ret;
}

int Compiler::assignElement(std::string const &ident, AddressOrInstruction const &index, AddressOrInstruction const &rhs)
{
    static std::string const dynamicMoveRight = "[>>[->+<]<[->+<]<[->+<]>-]";
    static std::string const dynamicMoveLeft = "[[-<+>]<-]<";
               
    int const arr = addressOf(ident);
    errorIf(arr < 0, "Variable \"", ident, "\" undefined in this scope.");
    
    int const sz = d_memory.sizeOf(arr);

    int const bufSize = sz + 2;
    int const buf = allocateTemp(bufSize);
    int const dist = buf - arr;

    std::string const arr2buf(std::abs(dist), (dist > 0 ? '>' : '<'));
    std::string const buf2arr(std::abs(dist), (dist > 0 ? '<' : '>'));

    d_codeBuffer << bf_assign(buf, index)
                 << bf_assign(buf + 1, buf)
                 << bf_assign(buf + 2, rhs)
                 << bf_setToValue(buf + 3, 0, bufSize - 3)
                 << bf_movePtr(buf)
                 << dynamicMoveRight
                 << buf2arr
                 << "[-]"
                 << arr2buf
                 << ">>"
                 << "["
                 <<     "-<<"
                 <<     buf2arr
                 <<     "+"
                 <<     arr2buf
                 <<     ">>"
                 << "]"
                 << "<"
                 << dynamicMoveLeft;
    
    // Attention: can't return the address of the modified cell, so we return the
    // address of the known RHS-cell.
    return rhs;
}


int Compiler::applyUnaryFunctionToElement(std::string const &arrayIdent,
                                          AddressOrInstruction const &index,
                                          UnaryFunction func)
{
    int const fetchedAddr = fetchElement(arrayIdent, index);
    int const returnAddr  = (this->*func)(fetchedAddr);
    assignElement(arrayIdent, index, fetchedAddr);
    return returnAddr;
}

int Compiler::applyBinaryFunctionToElement(std::string const &arrayIdent,
                                           AddressOrInstruction const &index,
                                           AddressOrInstruction const &rhs,
                                           BinaryFunction func)
{
    int const fetchedAddr = fetchElement(arrayIdent, index);
    int const returnAddr  = (this->*func)(fetchedAddr, rhs);
    assignElement(arrayIdent, index, fetchedAddr);
    return returnAddr;
}


int Compiler::preIncrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot increment void-expression.");

    d_codeBuffer << bf_incr(target);
    return target;
}

int Compiler::preDecrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot decrement void-expression.");
    
    d_codeBuffer << bf_decr(target);
    return target;
}

int Compiler::postIncrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot increment void-expression.");
    
    int const tmp = allocateTemp();
    d_codeBuffer << bf_assign(tmp, target)
                 << bf_incr(target);

    return tmp;
}

int Compiler::postDecrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot decrement void-expression.");

    int const tmp = allocateTemp();
    d_codeBuffer << bf_assign(tmp, target)
                 << bf_decr(target);

    return tmp;
}

int Compiler::addTo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");

    d_codeBuffer << bf_addTo(lhs, rhs);
    return lhs;
}

int Compiler::add(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");
    
    int const ret = allocateTemp();
    d_codeBuffer << bf_assign(ret, lhs)
                 << bf_addTo(ret, rhs);
    return ret;
}

int Compiler::subtractFrom(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");
    
    d_codeBuffer << bf_subtractFrom(lhs, rhs);
    return lhs;
}

int Compiler::subtract(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");

    int const ret = allocateTemp();
    d_codeBuffer << bf_assign(ret, lhs)
                 << bf_subtractFrom(ret, rhs);
    return ret;
}

int Compiler::multiplyBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");
    
    d_codeBuffer << bf_multiplyBy(lhs, rhs);
    return lhs;
}

int Compiler::multiply(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");

    int const ret = allocateTemp();
    d_codeBuffer << bf_multiply(lhs, rhs, ret);
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
    int const tmp = allocateTempBlock(6);
    int const tmp_loopflag  = tmp + 0;
    int const tmp_zeroflag  = tmp + 1;
    int const tmp_num       = tmp + 2;
    int const tmp_denom     = tmp + 3;
    int const result_div    = tmp + 4;
    int const result_mod    = tmp + 5;
    
    // Algorithm:
    // 1. Initialize result-cells to 0 and copy operands to temps
    // 2. In case the denominator is 0 (divide by zero), set the result to 255 (~inf)
    //    Set the loopflag to 0 in order to skip the calculating loop.
    // 3. In case the numerator is 0, the result of division is also zero. Set the remainder
    //    to the same value as the denominator.
    // 4. Enter the loop:
    //      *  On each iteration, decrement both the denominator and enumerator,
    //       until the denominator becomes 0.When this happens, increment result_div and
    //       reset result_mod to 0. Also, reset the denominator to its original value.
    //
    //    *  The loop is broken when the enumerator has become zero.
    //       By that time, we have counted how many times te denominator
    //       fits inside the enumerator (result_div), and how many is left (result_mod).

    d_codeBuffer << bf_setToValue(result_div, 0)             // 1
                 << bf_setToValue(result_mod, 0)
                 << bf_assign(tmp_num, num)
                 << bf_assign(tmp_denom, denom)
                 << bf_setToValue(tmp_loopflag, 1)
                 << bf_not(denom, tmp_zeroflag)
                 << "["                                      // 2
                 <<     bf_setToValue(tmp_loopflag, 0)
                 <<     bf_setToValue(result_div, 255)
                 <<     bf_setToValue(result_mod, 255)
                 <<     bf_setToValue(tmp_zeroflag, 0)
                 << "]"
                 << bf_not(num, tmp_zeroflag)
                 << "["                                      // 3
                 <<     bf_setToValue(tmp_loopflag, 0)
                 <<     bf_setToValue(result_div, 0)
                 <<     bf_setToValue(result_mod, 0)
                 <<     bf_setToValue(tmp_zeroflag, 0)
                 << "]"
                 << bf_movePtr(tmp_loopflag)
                 << "["                                      // 4
                 <<     bf_decr(tmp_num)
                 <<     bf_decr(tmp_denom)
                 <<     bf_incr(result_mod)
                 <<     bf_not(tmp_denom, tmp_zeroflag)
                 <<     "["
                 <<         bf_incr(result_div)
                 <<         bf_assign(tmp_denom, denom)
                 <<         bf_setToValue(result_mod, 0)
                 <<         bf_setToValue(tmp_zeroflag, 0)
                 <<     "]"
                 <<     bf_not(tmp_num, tmp_zeroflag)
                 <<     "["
                 <<         bf_setToValue(tmp_loopflag, 0)
                 <<         bf_setToValue(tmp_zeroflag, 0)
                 <<     "]"
                 <<     bf_movePtr(tmp_loopflag)
                 << "]";

    return {result_div, result_mod};
}


int Compiler::equal(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_equal(lhs, rhs, result);
    return result;
}

int Compiler::notEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_notEqual(lhs, rhs, result);
    return result;
}

int Compiler::less(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_less(lhs, rhs, result);
    return result;
}

int Compiler::greater(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_greater(lhs, rhs, result);
    return result;
}

int Compiler::lessOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_lessOrEqual(lhs, rhs, result);
    return result;
}

int Compiler::greaterOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_greaterOrEqual(lhs, rhs, result);
    return result;
}

int Compiler::logicalNot(AddressOrInstruction const &arg)
{
    errorIf(arg < 0, "Use of void-expression in not-operation.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_not(arg, result);

    return result;
}

int Compiler::logicalAnd(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in and-operation.");
    
    int const result = allocateTemp();
    d_codeBuffer << bf_and(lhs, rhs, result);
    return result;
}

int Compiler::logicalOr(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in or-operation.");

    int const result = allocateTemp();
    d_codeBuffer << bf_or(lhs, rhs, result);
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

    d_codeBuffer << bf_movePtr(ifFlag)
                 << "[";

    {
        d_scopeStack.pushSubScope();
        ifBody();
        std::string outOfScope = d_scopeStack.popSubScope();
        d_memory.freeLocals(outOfScope);
    }
    
    d_codeBuffer << bf_setToValue(ifFlag, 0)
                 << "]"
                 <<  bf_movePtr(elseFlag)
                 << "[";

    {
        d_scopeStack.pushSubScope();
        elseBody();
        std::string outOfScope = d_scopeStack.popSubScope();
        d_memory.freeLocals(outOfScope);
    }
    
    d_codeBuffer << bf_setToValue(elseFlag, 0)
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
    
    d_codeBuffer << bf_assign(flag, conditionAddr)
                 << "[";

    body();
    increment();
    d_codeBuffer << bf_assign(flag, condition())
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

    d_codeBuffer << bf_assign(flag, conditionAddr)
                 << "[";
    body();

    d_codeBuffer << bf_assign(flag, condition())
                 << "]";

    std::string outOfScope = d_scopeStack.popSubScope();
    popStack();

    d_memory.freeLocals(outOfScope);
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
        return d_parser.lineNr();
    else if (d_stage == Stage::CODEGEN)
        return d_instructionLineNr;

    assert(false && "Unreachable");
    return -1;
}

std::string Compiler::filename() const
{
    if (d_stage == Stage::PARSING)
        return d_parser.filename();
    else if (d_stage == Stage::CODEGEN)
        return d_instructionFilename;

    assert(false && "Unreachable");
    return "";
}
