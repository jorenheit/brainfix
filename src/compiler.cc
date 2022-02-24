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

Compiler::Compiler(std::string const &file, CellType type, std::vector<std::string> const &paths):
    MAX_INT(_MaxInt::get(type)),
    MAX_ARRAY_SIZE(MAX_INT - 5),
    d_scanner(file, ""),
    d_memory(TAPE_SIZE_INITIAL),
    d_includePaths(paths)
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


void Compiler::pushStream(std::string const &file)
{
    for (std::string const &path: d_includePaths)
    {
        try
        {
            d_scanner.pushStream(path + file);
            return;
        }
        catch(...)
        {}
    }

    compilerError("Could not find included file \"", file, "\".");
}

void Compiler::sync(int const addr)
{
    assert(d_constEvalEnabled && "Cannot sync when constant evaluation is disabled");

    bool const valueKnown = not d_memory.valueUnknown(addr);
    bool const synced     = d_memory.isSync(addr);
    if (valueKnown && !synced)
    {
        runtimeSetToValue(addr, d_memory.value(addr));
        // d_memory.setSync(addr, true);
        // d_codeBuffer << d_bfGen.setToValue(addr, d_memory.value(addr));
    }
}

bool Compiler::setConstEval(bool const enable)
{
    static int count = 0;
    if (enable)
    {
        assert(count != 0 && "Unbalanced enableConstEval/disableConstEval");
        if (--count == 0)
            d_constEvalEnabled = true;
    }
    else
    {
        ++count;
        if (not d_constEvalEnabled)
            return false;

        // Sync all variables that are currently in scope.
        std::vector<int> scopeCells = d_memory.cellsInScope(d_scope.current());
        for (int const addr: scopeCells)
        {
            sync(addr);
        }

        d_constEvalEnabled = false;
    }

    return d_constEvalEnabled;
}

bool Compiler::enableConstEval()
{
    return setConstEval(true);
}

bool Compiler::disableConstEval()
{
    return setConstEval(false);
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

    d_constEvalEnabled = true;
    
    d_stage = Stage::CODEGEN;
    call("main");
    d_stage = Stage::FINISHED;

    //    d_memory.dump();
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

void Compiler::addFunction(BFXFunction const &bfxFunc)
{
    errorIf(!validateFunction(bfxFunc), "Duplicate parameters used in the definition of function \"",
            bfxFunc.name(), "\".");

    auto result = d_functionMap.insert({bfxFunc.name(), bfxFunc});
    errorIf(!result.second,
            "Redefinition of function \"", bfxFunc.name(), "\" is not allowed.");
}

void Compiler::addStruct(std::string const &name,
                         std::vector<std::pair<std::string, TypeSystem::Type>> const &fields)
{
    std::set<std::string> set;
    auto duplicateField = [&](std::string const &str) -> bool
                          {
                              return !set.insert(str).second;
                          };
    
    for (auto const &pr: fields)
    {
        std::string const &fieldName = pr.first;
        errorIf(duplicateField(fieldName),
                "Field \"", fieldName, "\" previously declared in definition of struct \"", name, "\".");
        
        TypeSystem::Type const &type = pr.second;
        errorIf(!type.defined(),"Variable \'", fieldName, "\' declared with unknown (struct) type.");

        int const sz = type.size();
        errorIf(sz == 0, "Cannot declare field \"", fieldName, "\" of size 0.");
        errorIf(type.isIntType() && sz > MAX_ARRAY_SIZE,
                "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded in struct definition (got ", sz, ").");
    }

    // All OK, add to typesystem
    bool const added = TypeSystem::add(name, fields);
    errorIf(!added, "Struct ", name, " previously defined.");
}

bool Compiler::validateFunction(BFXFunction const &bfxFunc)
{
    // Check if parameters are unique
    auto const &params = bfxFunc.params();
    std::set<std::string> paramSet;
    for (auto const &p: params)
    {
        auto result = paramSet.insert(p.first);
        if (!result.second)
            return false;
    }
    return true;
}

void Compiler::addGlobals(std::vector<std::pair<std::string, TypeSystem::Type>> const &declarations)
{
    for (auto const &var: declarations)
    {
        std::string const &ident = var.first;
        TypeSystem::Type const &type = var.second;
        errorIf(type.size() <= 0, "Global declaration of \"", ident, "\" has invalid size specification.");
        d_memory.allocate(ident, "", type);
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

int Compiler::allocate(std::string const &ident, TypeSystem::Type type)
{
    int const addr = d_memory.allocate(ident, d_scope.current(), type);

    if (!d_returnExistingAddressOnAlloc)
    {
        errorIf(addr < 0, "Variable ", ident, ": variable previously declared.");
    }
    else if (addr < 0)
    {
        return d_memory.find(ident, d_scope.current());
    }

    return addr;
}

int Compiler::addressOf(std::string const &ident)
{
    int addr = d_memory.find(ident, d_scope.current());
    addr = (addr != -1) ? addr : d_memory.find(ident, "");
    errorIf(addr < 0, "Variable \"", ident, "\" not defined in this scope.");
    return addr;
}

int Compiler::allocateTemp(TypeSystem::Type type)
{
    return d_memory.getTemp(d_scope.function(), type);
}

int Compiler::allocateTemp(int const sz)
{
    return d_memory.getTemp(d_scope.function(), sz);
}

int Compiler::allocateTempBlock(int const sz)
{
    return d_memory.getTempBlock(d_scope.function(), sz);
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
    int const sz = d_memory.sizeOf(ident, d_scope.current());
    errorIf(sz == 0, "Variable \"", ident ,"\" not defined in this scope.");

    return constVal(sz);
}

int Compiler::movePtr(std::string const &ident)
{
    int const addr = addressOf(ident);
    d_codeBuffer << d_bfGen.movePtr(addr);
    return -1;
}

int Compiler::statement(Instruction const &instr)
{
    instr();
    d_memory.freeTemps(d_scope.current());
    return -1;
}

int Compiler::call(std::string const &name, std::vector<Instruction> const &args)
{
    // Check if the function exists
    bool const isFunction = d_functionMap.find(name) != d_functionMap.end();
    errorIf(!isFunction,"Call to unknown function \"", name, "\"");
    errorIf(d_scope.containsFunction(name),
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
        BFXFunction::Parameter const &p          = params[idx];
        std::string const            &paramIdent = p.first;
        BFXFunction::ParameterType    paramType  = p.second;
        if (paramType == BFXFunction::ParameterType::Value)
        {
            // Allocate local variable for the function of the correct size
            // and copy argument to this location
            //            int const sz = d_memory.sizeOf(argAddr);

            int paramAddr = d_memory.allocate(paramIdent, func.name(), d_memory.type(argAddr));
            errorIf(paramAddr < 0,
                    "Could not allocate parameter", paramIdent, ". ",
                    "This should never happen.");
            
            assign(paramAddr, argAddr);
        }
        else // Reference
        {
            references.push_back(argAddr);
            d_memory.push(argAddr);
            d_memory.rename(argAddr, paramIdent, func.name());
        }
    }

    // Execute body of the function
    d_scope.pushFunction(func.name());
    func.body()();
    d_scope.popFunction();
    

    // Move return variable to local scope before cleaning up (if non-void)
    int ret = -1;
    if (!func.isVoid())
    {
        // Locate the address of the return-variable
        std::string retVar = func.returnVariable();
        ret = d_memory.find(retVar, func.name());
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
            d_memory.rename(ret, "", d_scope.current());
            d_memory.markAsTemp(ret);
        }
    }

    // Pull referenced arguments back into original scope
    for (size_t i = 0; i != references.size(); ++i)
        d_memory.pop();
    
    // Clean up and return
    d_memory.freeLocals(func.name());
    return ret;
}

int Compiler::constVal(int const num)
{
    warningIf(num > MAX_INT, "use of value ", num, " exceeds limit of ", MAX_INT, ".");
    
    int const tmp = allocateTemp();
    constEvalSetToValue(tmp, num);

    if (!d_constEvalEnabled)
    {
        d_memory.setSync(tmp, true);
        d_codeBuffer << d_bfGen.setToValue(tmp, num);
    }
    return tmp;
}

int Compiler::declareVariable(std::string const &ident, TypeSystem::Type type)
{
    errorIf(!type.defined(), "Variable \'", ident, "\' declared with unknown type.");

    
    int const sz = type.size();
    errorIf(sz == 0, "Cannot declare variable \"", ident, "\" of size 0.");
    errorIf(sz < 0,
            "Size must be specified in declaration without initialization of variable ", ident);

    errorIf(type.isIntType() && sz > MAX_ARRAY_SIZE,
                "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    return allocate(ident, type);
}

int Compiler::initializeExpression(std::string const &ident, TypeSystem::Type type, Instruction const &rhs)
{
    errorIf(!type.defined(), "Variable \'", ident, "\' declared with unknown type.");

    int const sz = type.size();
    errorIf(sz == 0, "Cannot declare variable \"", ident, "\" of size 0.");
    errorIf(type.isIntType() && sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", (int)sz, ").");

    if (!d_returnExistingAddressOnAlloc)
        errorIf(d_memory.find(ident, d_scope.current()) != -1,
                "Variable ", ident, " previously declared.");
    
    int rhsAddr = rhs();
    TypeSystem::Type rhsType = d_memory.type(rhsAddr);
    
    if (d_memory.isTemp(rhsAddr) && (sz == -1 || type == rhsType))
    {
        d_memory.rename(rhsAddr, ident, d_scope.current());
        return rhsAddr;
    }
    else if (type == rhsType || (type.isIntType() && rhsType.isIntType()))
    {
        return assign(allocate(ident, type), rhsAddr);
    }

    compilerError("Type mismatch in assignment of \"", rhsType.name(),
                  "\" to variable \"", ident, "\" of type \"", type.name(), "\"." );
    return -1;
}

void Compiler::constEvalSetToValue(int const addr, int const val)
{
    d_memory.setSync(addr, false);
    d_memory.value(addr) = val;
}

void Compiler::runtimeSetToValue(int const addr, int const newVal)
{
    if (d_memory.valueUnknown(addr) || !d_memory.isSync(addr))
    {
        d_codeBuffer << d_bfGen.setToValue(addr, newVal);
    }
    else 
    {
        int const oldVal = d_memory.value(addr);
        int const diff = newVal - oldVal;
        if (std::abs(diff) < newVal)
            d_codeBuffer << d_bfGen.addConst(addr, newVal - oldVal);
        else
            d_codeBuffer << d_bfGen.setToValue(addr, newVal);
    }

    d_memory.value(addr) = newVal;
    d_memory.setSync(addr, true);
}

int Compiler::assign(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void expression in assignment.");
    
    int const leftSize = d_memory.sizeOf(lhs);
    int const rightSize = d_memory.sizeOf(rhs);
    
    if (leftSize > 1 && rightSize == 1)
    {
        // Fill array with value
        if (d_constEvalEnabled && !d_memory.valueUnknown(rhs))
        {
            for (int i = 0; i != leftSize; ++i)
            {
                constEvalSetToValue(lhs + i, d_memory.value(rhs));
            }
        }
        else
        {
            for (int i = 0; i != leftSize; ++i)
            {
                d_codeBuffer << d_bfGen.assign(lhs + i, rhs);
                d_memory.setValueUnknown(lhs + i);
            }
        }
    }
    else if (leftSize == rightSize)
    {
        // Same size -> copy
        if (d_constEvalEnabled)
        {
            for (int i = 0; i != leftSize; ++i)
            {
                if (!d_memory.valueUnknown(rhs + i))
                {
                    constEvalSetToValue(lhs + i, d_memory.value(rhs + i));
                }
                else
                {
                    d_codeBuffer << d_bfGen.assign(lhs + i, rhs + i);
                    d_memory.setValueUnknown(lhs + i);
                }
            }
        }
        else
        {
            d_codeBuffer << d_bfGen.assign(lhs, rhs, leftSize);
            for (int i = 0; i != leftSize; ++i)
                d_memory.setValueUnknown(lhs + i);
        }
    }
    else if (leftSize == 1 && !d_memory.type(rhs).isStructType())
    {
        if (d_constEvalEnabled && !d_memory.valueUnknown(rhs))
        {
            constEvalSetToValue(lhs, d_memory.value(rhs));
        }
        else
        {
            d_codeBuffer << d_bfGen.assign(lhs, rhs);
            d_memory.setValueUnknown(lhs);
        }
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
    return addr;
}

int Compiler::fetchField(std::vector<std::string> const &expr)
{
    assert(expr.size() > 1 && "Got field with less than 2 elements");
    
    int const addr = addressOf(expr[0]);
    errorIf(addr < 0, "Unknown variable \"", expr[0], "\".");

    TypeSystem::Type type = d_memory.type(addr);
    errorIf(!type.isStructType(), "Type \"", type.name(), "\" is not a structure.");

    auto def = type.definition();
    for (auto const &f: def.fields())
    {
        if (f.name == expr[1])
        {
            if (expr.size() == 2)
            {
                return (addr + f.offset);
            }
            else
            {
                return fetchNestedField(expr, addr + f.offset, 1);
            }
        }
    }

    compilerError("Structure \"", type.name(), "\" does not contain field \"", expr[1], "\".");
    return -1;
}

int Compiler::fetchNestedField(std::vector<std::string> const &expr, int const baseAddr, size_t const baseIdx)
{
    TypeSystem::Type type = d_memory.type(baseAddr);
    errorIf(!type.isStructType(), "Type \"", type.name(), "\" is not a structure.");
    
    auto def = type.definition();
    for (auto const &f: def.fields())
    {
        if (f.name == expr[baseIdx + 1])
        {
            if (baseIdx + 2 == expr.size())
            {
                return (baseAddr + f.offset);
            }
            else
            {
                return fetchNestedField(expr, baseAddr + f.offset, baseIdx + 1);
            }
        }
    }

    compilerError("Structure \"", type.name(), "\" does not contain field \"", expr[baseIdx + 1], "\".");
    return -1;
}

int Compiler::arrayFromSize(int const sz, Instruction const &fill)
{
    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    return assign(allocateTemp(sz), fill());
}

int Compiler::arrayFromList(std::vector<Instruction> const &list)
{
    int const sz = list.size();

    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    std::vector<std::pair<int, int>> runtimeElements; // {index, address}
    int start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
    {
        int const elementAddr = list[idx]();
        if (d_constEvalEnabled && !d_memory.valueUnknown(elementAddr))
        {
            constEvalSetToValue(start + idx, d_memory.value(elementAddr));
        }
        else
            runtimeElements.push_back({idx, elementAddr});
    }

    for (auto const &pr: runtimeElements)
    {
        int const elementIdx = pr.first;
        int const elementAddr = pr.second;
        d_codeBuffer << d_bfGen.assign(start + elementIdx, elementAddr);
        d_memory.setValueUnknown(start + elementIdx);
    }

    return start;
}

int Compiler::arrayFromString(std::string const &str)
{
    int const sz = str.size();

    errorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    int const start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
    {
        constEvalSetToValue(start + idx, str[idx]);
        if (!d_constEvalEnabled)
        {
            runtimeSetToValue(start + idx, str[idx]);
            // d_memory.setSync(start + idx, true);
            // d_codeBuffer << d_bfGen.setToValue(start + idx, str[idx]);
        }
    }

    return start;
}

int Compiler::anonymousStructObject(std::string const name, std::vector<Instruction> const &values)
{
    TypeSystem::Type type(name);
    errorIf(!type.defined(), "Unknown (struct) type \"", name, "\".");

    auto const def = type.definition();
    errorIf(values.size() > def.fields().size(),
            "Too many field-initializers provided to struct \"", name, "\": ",
            "expects ", def.fields().size(), ", got ", values.size(), ".");

    int const addr = allocateTemp(type);
    for (size_t i = 0; i != values.size(); ++i)
    {
        auto const &field = def.fields()[i];
        
        int val = values[i]();
        TypeSystem::Type valType = d_memory.type(val);
        TypeSystem::Type fieldType = field.type;
        
        errorIf(!(valType == fieldType),
                "Type mismatch in initialization of \"", name , ".", field.name, "\".");

        assign(addr + field.offset, val);
    }

    return addr;
}


int Compiler::fetchElement(AddressOrInstruction const &arr, AddressOrInstruction const &index)
{
    // TODO: implement compile-time bounds checking

    if (d_constEvalEnabled && !d_memory.valueUnknown(index))
    {
        return arr + d_memory.value(index);
    }
    else
    {
        int const sz = d_memory.sizeOf(arr);
        int const ret = allocateTemp();
        d_codeBuffer << d_bfGen.fetchElement(arr, sz, index, ret);
        d_memory.setValueUnknown(ret);
        return ret;
    }
}

int Compiler::assignElement(AddressOrInstruction const &arr, AddressOrInstruction const &index, AddressOrInstruction const &rhs)
{
    if (d_constEvalEnabled && !d_memory.valueUnknown(index) && !d_memory.valueUnknown(rhs))
    {
        // Case 1: index and rhs both known
        int const addr = arr + d_memory.value(index);
        constEvalSetToValue(addr, d_memory.value(rhs));
        return addr;
    }
    else if (d_constEvalEnabled && !d_memory.valueUnknown(index))
    {
        // Case 2: only index known
        int const addr = arr + d_memory.value(index);

        d_codeBuffer << d_bfGen.assign(addr, rhs);
        d_memory.setValueUnknown(addr);
        return addr;
    }
    else
    {
        // Case 3: Index unkown or constant evaluation is disabled ->
        // make sure index and rhs are synced and use full algorithm .
        // This may alter any of the array elements -> set all elements to unknown status.
        
        if (d_constEvalEnabled)
        {
            sync(index);
            sync(rhs);
        }
        
        int const sz = d_memory.sizeOf(arr);
        d_codeBuffer << d_bfGen.assignElement(arr, sz, index, rhs);
        for (int i = 0; i != sz; ++i)
            d_memory.setValueUnknown(arr + i);

        // Attention: can't return the address of the modified cell, so we return the
        // address of the known RHS-cell.
        // TODO: check if returning -1 (void) works out
        return rhs;
    }
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

int Compiler::scanCell(std::string const &ident)
{
    int const addr = addressOf(ident);
    d_codeBuffer << d_bfGen.scan(addr);
    d_memory.setValueUnknown(addr);
    return addr;
}

int Compiler::printCell(AddressOrInstruction const &target)
{
    if (d_constEvalEnabled)
        sync(target);
    
    d_codeBuffer << d_bfGen.print(target);
    return target;
}

int Compiler::preIncrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot increment void-expression.");
    
    auto bf   = [&, this](){
                    d_codeBuffer << d_bfGen.incr(target);
                };
    auto func = [](int x){ return ++x; };
    
    return eval<0b0>(bf, func, target, target);
}

int Compiler::postIncrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot increment void-expression.");

    int const tmp = allocateTemp();
    auto bf   = [&, this](){
                    d_codeBuffer << d_bfGen.assign(tmp, target)
                                 << d_bfGen.incr(target);
                };
    auto func = [](int &x){ return x++; };

    return eval<0b1>(bf, func, tmp, target);
}

int Compiler::preDecrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot decrement void-expression.");

    auto bf   = [&, this](){
                    d_codeBuffer << d_bfGen.decr(target);
                };
    auto func = [](int x){ return --x; };
    
    return eval<0b0>(bf, func, target, target);
}


int Compiler::postDecrement(AddressOrInstruction const &target)
{
    errorIf(target < 0, "Cannot decrement void-expression.");

    int const tmp = allocateTemp();
    auto bf   = [&, this](){
                    d_codeBuffer << d_bfGen.assign(tmp, target)
                                 << d_bfGen.incr(target);
                };
    auto func = [](int &x){ return x--; };

    return eval<0b1>(bf, func, tmp, target);
}

int Compiler::addTo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");

    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.addTo(lhs, rhs);
               };

    auto func = [](int x, int y){
                    return x + y;
                };

    return eval<0b00>(bf, func, lhs, lhs, rhs);
}

int Compiler::add(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.assign(ret, lhs)
                                << d_bfGen.addTo(ret, rhs);
               };

    auto func = [](int x, int y){
                    return x + y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
}

int Compiler::subtractFrom(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");

    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.subtractFrom(lhs, rhs);
               };

    auto func = [](int x, int y){
                    return x - y;
                };

    return eval<0b00>(bf, func, lhs, lhs, rhs);
    
}

int Compiler::subtract(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.assign(ret, lhs)
                                << d_bfGen.subtractFrom(ret, rhs);
               };

    auto func = [](int x, int y){
                    return x - y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::multiplyBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.multiplyBy(lhs, rhs);
               };

    auto func = [](int x, int y){
                    return x * y;
                };

    return eval<0b00>(bf, func, lhs, lhs, rhs);
}


int Compiler::multiply(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.multiply(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x * y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::divide(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   int const dummy = allocateTemp();
                   divModPair(lhs, rhs, ret, dummy);
               };

    auto func = [](int x, int y){
                    return x / y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
}

int Compiler::divideBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");

    auto bf  = [&, this](){
                   int const div = allocateTemp();
                   int const dummy = allocateTemp();
                   divModPair(lhs, rhs, div, dummy);
                   assign(lhs, div);
               };

    auto func = [](int x, int y){
                    return x / y;
                };

    return eval<0b00>(bf, func, lhs, lhs, rhs);
    
}

int Compiler::modulo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in modulo-operation.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   int const dummy = allocateTemp();
                   divModPair(lhs, rhs, dummy, ret);
               };

    auto func = [](int x, int y){
                    return x % y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
}

int Compiler::moduloBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in modulo-operation.");

    auto bf  = [&, this](){
                   int const mod = allocateTemp();
                   int const dummy = allocateTemp();
                   divModPair(lhs, rhs, dummy, mod);
                   assign(lhs, mod);
               };

    auto func = [](int x, int y){
                    return x % y;
                };

    return eval<0b00>(bf, func, lhs, lhs, rhs);
    
}


int Compiler::divMod(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in divmod-operation.");

    int const mod = allocateTemp();
    auto bf  = [&, this](){
                   int const div = allocateTemp();
                   divModPair(lhs, rhs, div, mod);
                   assign(lhs, div);
               };

    auto func = [](int &x, int y){
                    int ret = x % y; 
                    x /= y;
                    return ret;
                };

    return eval<0b10>(bf, func, mod, lhs, rhs);
    
}

int Compiler::modDiv(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in moddiv-operation.");

    int const div = allocateTemp();
    auto bf  = [&, this](){
                   int const mod = allocateTemp();
                   divModPair(lhs, rhs, div, mod);
                   assign(lhs, mod);
               };

    auto func = [](int &x, int y){
                    int ret = x / y; 
                    x %= y;
                    return ret;
                };

    return eval<0b10>(bf, func, div, lhs, rhs);
    
}

void Compiler::divModPair(AddressOrInstruction const &num, AddressOrInstruction const &denom, int const divResult, int const modResult)
{
    d_codeBuffer << d_bfGen.divmod(num, denom, divResult, modResult);
    d_memory.setValueUnknown(divResult);
    d_memory.setValueUnknown(modResult);
}


int Compiler::equal(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.equal(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x == y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::notEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.notEqual(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x != y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::less(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.less(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x < y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::greater(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.greater(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x > y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::lessOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.lessOrEqual(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x <= y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
}

int Compiler::greaterOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.greaterOrEqual(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x >= y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::logicalNot(AddressOrInstruction const &arg)
{
    errorIf(arg < 0, "Use of void-expression in not-operation.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.logicalNot(arg, ret);
               };

    auto func = [](int x){
                    return !x;
                };

    return eval<0b0>(bf, func, ret, arg);
    
}

int Compiler::logicalAnd(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in and-operation.");
    
    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.logicalAnd(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x && y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::logicalOr(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    errorIf(lhs < 0 || rhs < 0, "Use of void-expression in or-operation.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.logicalOr(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x || y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::ifStatement(Instruction const &condition, Instruction const &ifBody, Instruction const &elseBody)
{
    int const conditionAddr = condition();
    errorIf(conditionAddr < 0, "Use of void-expression in if-condition.");

    if (d_constEvalEnabled && !d_memory.valueUnknown(conditionAddr))
    {
        d_scope.push();
        (d_memory.value(conditionAddr) > 0 ? ifBody : elseBody)();
        std::string const outOfScope = d_scope.pop();
        d_memory.freeLocals(outOfScope);
        return -1;
    }

    disableConstEval();
    
    int const ifFlag = allocateTemp();
    assign(ifFlag, conditionAddr);
    int const elseFlag = logicalNot(ifFlag);


    d_codeBuffer << d_bfGen.movePtr(ifFlag)
                 << "[";

    {
        d_scope.push();
        ifBody();
        std::string const outOfScope = d_scope.pop();
        d_memory.freeLocals(outOfScope);
    }
    
    d_codeBuffer << d_bfGen.setToValue(ifFlag, 0)
                 << "]"
                 << d_bfGen.movePtr(elseFlag)
                 << "[";

    {
        d_scope.push();
        elseBody();
        std::string const outOfScope = d_scope.pop();
        d_memory.freeLocals(outOfScope);
    }
    
    d_codeBuffer << d_bfGen.setToValue(elseFlag, 0)
                 << "]";

    enableConstEval();
    
    return -1;
}

int Compiler::mergeInstructions(Instruction const &instr1, Instruction const &instr2)
{
    instr1();
    instr2();

    return -1;
}

int Compiler::forStarStatement(Instruction const &init, Instruction const &condition,
                               Instruction const &increment, Instruction const &body)
{
    if (!d_constEvalEnabled)
        return forStatement(init, condition, increment, body);

    d_scope.push();
    
    init();
    int conditionAddr = condition();
    errorIf(d_memory.valueUnknown(conditionAddr),
            "Condition could not be const-evaluated on entering for*.");

    int count = 0;
    while (d_memory.value(conditionAddr))
    {
        body();
        increment();
        conditionAddr = condition();
        errorIf(d_memory.valueUnknown(conditionAddr),
                "Condition could not be const-evaluated while iterating for* at iteration ", count, ".");

        d_returnExistingAddressOnAlloc = true;

        if (count++ > MAX_LOOP_UNROLL_ITERATIONS)
            compilerError("Error unrolling loop: number of iterations exceeds maximum of ",
                          MAX_LOOP_UNROLL_ITERATIONS, ". Consider using `for` instead of `for*`.");
    }
    d_returnExistingAddressOnAlloc = false;
    
    std::string outOfScope = d_scope.pop();
    d_memory.freeLocals(outOfScope);

    return -1;
}

int Compiler::forStatement(Instruction const &init, Instruction const &condition,
                           Instruction const &increment, Instruction const &body)
{
    int const flag = allocateTemp();
    d_scope.push();
    disableConstEval();

    init();
    int const conditionAddr = condition();
    errorIf(conditionAddr < 0, "Use of void-expression in for-condition.");

    d_codeBuffer << d_bfGen.assign(flag, conditionAddr);
    d_codeBuffer << "[";

    body();
    increment();
    d_codeBuffer << d_bfGen.assign(flag, condition())
                 << "]";

    std::string outOfScope = d_scope.pop();
    d_memory.freeLocals(outOfScope);

    enableConstEval();
    return -1;
}

int Compiler::whileStarStatement(Instruction const &condition, Instruction const &body)
{
    if (!d_constEvalEnabled)
        return whileStatement(condition, body);

    d_scope.push();
    
    int conditionAddr = condition();
    errorIf(d_memory.valueUnknown(conditionAddr),
            "Condition could not be const-evaluated on entering while*.");

    int count = 0;
    while (d_memory.value(conditionAddr))
    {
        body();
        conditionAddr = condition();
        errorIf(d_memory.valueUnknown(conditionAddr),
                "Condition could not be const-evaluated while iterating while* at iteration ", count, ".");

        d_returnExistingAddressOnAlloc = true;

        if (count++ > MAX_LOOP_UNROLL_ITERATIONS)
            compilerError("Error unrolling loop: number of iterations exceeds maximum of ",
                          MAX_LOOP_UNROLL_ITERATIONS, ". Consider using `while` instead of `while*`.");
    }
    d_returnExistingAddressOnAlloc = false;
    
    std::string outOfScope = d_scope.pop();
    d_memory.freeLocals(outOfScope);

    return -1;    
}

int Compiler::whileStatement(Instruction const &condition, Instruction const &body)
{
    int const conditionAddr = condition();
    errorIf(conditionAddr < 0, "Use of void-expression in while-condition.");

    d_scope.push();
    disableConstEval();
    d_codeBuffer << d_bfGen.movePtr(conditionAddr)
                 << "[";
    body();

    d_codeBuffer << d_bfGen.assign(conditionAddr, condition())
                 << "]";

    std::string outOfScope = d_scope.pop();
    d_memory.freeLocals(outOfScope);
    enableConstEval();
    return -1;
}

int Compiler::switchStatement(Instruction const &compareExpr,
                              std::vector<std::pair<Instruction, Instruction>> const &cases,
                              Instruction const &defaultCase)
{
    std::vector<std::pair<int, int>> runtimeCases; // {index, caseAddr}
    
    int const compareAddr = compareExpr();
    if (d_constEvalEnabled && !d_memory.valueUnknown(compareAddr))
    {
        int const compareVal = d_memory.value(compareAddr);
        for (size_t i = 0; i != cases.size(); ++i)
        {
            int const caseAddr = cases[i].first();
            if (d_memory.valueUnknown(caseAddr))
            {
                runtimeCases.push_back({i, caseAddr});
                continue;
            }
            
            if (d_memory.value(caseAddr) == compareVal)
            {
                // Case match! execute case body
                d_scope.push();
                cases[i].second();
                std::string const outOfScope = d_scope.pop();
                d_memory.freeLocals(outOfScope);
                return -1;
            }
        }

        if (runtimeCases.size() == 0)
        {
            // all cases handled, no match -> handle default case
            d_scope.push();
            defaultCase();
            std::string const outOfScope = d_scope.pop();
            d_memory.freeLocals(outOfScope);
            return -1;
        }
    }
    else
    {
        // All cases are runtime-cases
        for (size_t i = 0; i != cases.size(); ++i)
            runtimeCases.push_back({i, cases[i].first()});
    }

    // No match found during compiletime --> handle runtime cases
    disableConstEval();
    int const goToDefault = allocateTemp();
    int const flag = allocateTemp();

    d_codeBuffer << d_bfGen.setToValue(goToDefault, 1);
    for (auto const &pr: runtimeCases)
    {
        int const caseIdx  = pr.first;
        int const caseAddr = pr.second;
        
        d_codeBuffer << d_bfGen.equal(compareAddr, caseAddr, flag)
                     << "["
                     <<     d_bfGen.setToValue(goToDefault, 0);

        d_scope.push();
        cases[caseIdx].second();
        std::string const outOfScope = d_scope.pop();
        d_memory.freeLocals(outOfScope);

        d_codeBuffer << d_bfGen.setToValue(flag, 0)
                     << "]";

    }

    d_codeBuffer << d_bfGen.movePtr(goToDefault)
                 << "[";

    d_scope.push();
    defaultCase();
    std::string const outOfScope = d_scope.pop();
    d_memory.freeLocals(outOfScope);

    d_codeBuffer << d_bfGen.setToValue(goToDefault, 0)
                 << "]";

    enableConstEval();
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
