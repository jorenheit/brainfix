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

Compiler::Compiler(Options const &opt):
    MAX_INT(_MaxInt::get(opt.cellType)),
    MAX_ARRAY_SIZE(MAX_INT - 5),
    d_scanner(opt.bfxFile, ""),
    d_memory(TAPE_SIZE_INITIAL),
    d_includePaths(opt.includePaths),
    d_constEvalEnabled(opt.constEvalEnabled),
    d_randomExtensionEnabled(opt.randomEnabled),
    d_bcrEnabled(opt.bcrEnabled),
    d_includeWarningEnabled(opt.includeWarningEnabled),
    d_outStream(*opt.outStream)
{
    d_includePaths.push_back(BFX_DEFAULT_INCLUDE_PATH_STRING);
    d_included.push_back(fileWithoutPath(opt.bfxFile));
    
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

Compiler::State Compiler::save()
{
    return {
            .memory         = d_memory,
            .scope          = d_scope,
            .bfGen          = d_bfGen,
            .buffer         = d_codeBuffer.str(),
            .constEval      = d_constEvalEnabled,
            .returnExisting = d_returnExistingAddressOnAlloc,
            .boundsChecking = d_boundsCheckingEnabled,
            .bcrMap         = d_bcrMap,
    };
}

void Compiler::restore(State &&state)
{
    d_memory                       = std::move(state.memory);
    d_scope                        = std::move(state.scope);
    d_bfGen                        = std::move(state.bfGen);
    d_bcrMap                       = std::move(state.bcrMap);
    d_constEvalEnabled             = state.constEval;
    d_returnExistingAddressOnAlloc = state.returnExisting;
    d_boundsCheckingEnabled        = state.boundsChecking;
    d_codeBuffer.str(state.buffer);
    d_codeBuffer.seekp(0, std::ios_base::end);
}

void Compiler::disableBoundChecking()
{
    d_boundsCheckingEnabled = false;
}

void Compiler::enableBoundChecking()
{
    d_boundsCheckingEnabled = true;
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

std::string Compiler::fileWithoutPath(std::string const &file)
{
    size_t const pos = file.find_last_of("/\\");
    std::string result = (pos == std::string::npos) ? file : file.substr(pos + 1);
    return result;
}

void Compiler::pushStream(std::string const &file)
{
    std::string const shortFile = fileWithoutPath(file);
    if (std::find(d_included.begin(), d_included.end(), shortFile) != d_included.end())
    {
        compilerWarningIf(d_includeWarningEnabled,
                          "Multiple inclusion of file ", shortFile, ". Duplicate filenames "
                          "will be ignored even if they are different files. You can disable "
                          "this warning with --no-multiple-inclusion-warning.");
        
        return; // ignore circular inclusions
    }
    
    for (std::string const &path: d_includePaths)
    {
        try
        {
            d_scanner.pushStream(path + '/' + file);
            d_included.push_back(shortFile);
            return;
        }
        catch(...)
        {}
    }

    compilerError("Could not find included file \"", file, "\".");
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
            sync(addr);

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
    
    compilerErrorIf(d_functionMap.find("main") == d_functionMap.end(),
            "No entrypoint provided. The entrypoint should be main().");

    d_stage = Stage::CODEGEN;
    call("main");
    d_stage = Stage::FINISHED;

    //    d_memory.dump();
    return 0;
}

void Compiler::write()
{
    d_outStream << cancelOppositeCommands(d_codeBuffer.str()) << '\n';
}

void Compiler::error()
{
    std::cerr << "ERROR: Syntax error on line "
              << d_scanner.lineNr() << " of file "
              << d_scanner.filename() << '\n';
}

void Compiler::addFunction(BFXFunction const &bfxFunc)
{
    compilerErrorIf(!validateFunction(bfxFunc), "Duplicate parameters used in the definition of function \"",
            bfxFunc.name(), "\".");

    auto result = d_functionMap.insert({bfxFunc.name(), bfxFunc});
    compilerErrorIf(!result.second,
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
        compilerErrorIf(duplicateField(fieldName),
                "Field \"", fieldName, "\" previously declared in definition of struct \"", name, "\".");
        
        TypeSystem::Type const &type = pr.second;
        compilerErrorIf(!type.defined(),"Variable \'", fieldName, "\' declared with unknown (struct) type.");

        int const sz = type.size();
        compilerErrorIf(sz == 0, "Cannot declare field \"", fieldName, "\" of size 0.");
        compilerErrorIf(type.isIntType() && sz > MAX_ARRAY_SIZE,
                "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded in struct definition (got ", sz, ").");
    }

    // All OK, add to typesystem
    bool const added = TypeSystem::add(name, fields);
    compilerErrorIf(!added, "Struct ", name, " previously defined.");
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
        compilerErrorIf(type.size() <= 0, "Global declaration of \"", ident, "\" has invalid size specification.");
        d_memory.allocate(ident, "", type);
    }
}

void Compiler::addConstant(std::string const &ident, int const num)
{
    compilerWarningIf(num > MAX_INT, "use of value ", num, " exceeds limit of ", MAX_INT, ".");
    auto result = d_constMap.insert({ident, num});
    compilerErrorIf(!result.second,
            "Redefinition of constant ", ident, " is not allowed.");
}

int Compiler::compileTimeConstant(std::string const &ident) const
{
    compilerErrorIf(!isCompileTimeConstant(ident),
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
        compilerErrorIf(addr < 0, "Variable ", ident, ": variable previously declared.");
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
    compilerErrorIf(addr < 0, "Variable \"", ident, "\" not defined in this scope.");
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

int Compiler::sizeOfOperator(std::string const &ident)
{
    int const sz = d_memory.sizeOf(ident, d_scope.current());
    compilerErrorIf(sz == 0, "Variable \"", ident ,"\" not defined in this scope.");

    return constVal(sz);
}

int Compiler::statement(Instruction const &instr)
{
    if (d_bcrEnabled)
    {
        int const continueFlag = getCurrentContinueFlag();
        int const breakFlag = getCurrentBreakFlag();

        Instruction const condition = [&, this](){ return logicalAnd(continueFlag, breakFlag); };
        Instruction const elseBody = [](){ return -1; };
        ifStatement(condition, instr, elseBody, false);
    }
    else
    {
        instr();
    }
    
    d_memory.freeTemps(d_scope.current());
    return -1;
}

int Compiler::call(std::string const &name, std::vector<Instruction> const &args)
{
    // Check if the function exists
    bool const isFunction = d_functionMap.find(name) != d_functionMap.end();
    compilerErrorIf(!isFunction,"Call to unknown function \"", name, "\"");
    compilerErrorIf(d_scope.containsFunction(name),
            "Function \"", name, "\" is called recursively. Recursion is not allowed.");
    
    // Get the list of parameters
    BFXFunction &func = d_functionMap.at(name);
    auto const &params = func.params();
    compilerErrorIf(params.size() != args.size(),
            "Calling function \"", func.name(), "\" with invalid number of arguments. "
            "Expected ", params.size(), ", got ", args.size(), ".");

    std::vector<int> references;
    for (size_t idx = 0; idx != args.size(); ++idx)
    {
        // Evaluate argument that's passed in and get its size
        int const argAddr = args[idx]();
        compilerErrorIf(argAddr < 0,
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
            compilerErrorIf(paramAddr < 0,
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
    enterScope(func.name());
    func.body()();
    exitScope(func.name());
    

    // Move return variable to local scope before cleaning up (if non-void)
    int ret = -1;
    if (!func.isVoid())
    {
        // Locate the address of the return-variable
        std::string retVar = func.returnVariable();
        ret = d_memory.find(retVar, func.name());
        compilerErrorIf(ret == -1,
                "Returnvalue \"", retVar, "\" of function \"", func.name(),
                "\" seems not to have been declared in the main scope of the function-body.");

        // Check if the return variable was passed into the function as a reference
        bool const returnVariableIsReferenceParameter =
            std::any_of(references.begin(), references.end(), [&](int addr){
                                                                  return addr == ret;
                                                              });
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
    compilerWarningIf(num > MAX_INT, "use of value ", num, " exceeds limit of ", MAX_INT, ".");
    
    int const tmp = allocateTemp();
    constEvalSetToValue(tmp, num);

    if (!d_constEvalEnabled)
        runtimeSetToValue(tmp, num);

    return tmp;
}

int Compiler::declareVariable(std::string const &ident, TypeSystem::Type type)
{
    compilerErrorIf(!type.defined(), "Variable \'", ident, "\' declared with unknown type.");
    
    int const sz = type.size();
    compilerErrorIf(sz == 0, "Cannot declare variable \"", ident, "\" of size 0.");
    compilerErrorIf(sz < 0,
            "Size must be specified in declaration without initialization of variable ", ident);

    compilerErrorIf(type.isIntType() && sz > MAX_ARRAY_SIZE,
                "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    return allocate(ident, type);
}

int Compiler::initializeExpression(std::string const &ident, TypeSystem::Type type, Instruction const &rhs)
{
    compilerErrorIf(!type.defined(), "Variable \'", ident, "\' declared with unknown type.");

    int const sz = type.size();
    compilerErrorIf(sz == 0, "Cannot declare variable \"", ident, "\" of size 0.");
    compilerErrorIf(type.isIntType() && sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", (int)sz, ").");

    if (!d_returnExistingAddressOnAlloc)
        compilerErrorIf(d_memory.find(ident, d_scope.current(), false) != -1,
                "Variable ", ident, " previously declared.");
    
    int rhsAddr = rhs();
    compilerErrorIf(rhsAddr < 0, "Use of void expression in assignment.");
    
    TypeSystem::Type rhsType = d_memory.type(rhsAddr);
    
    if (d_memory.isTemp(rhsAddr) && (sz == -1 || type == rhsType) && !d_returnExistingAddressOnAlloc)
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

void Compiler::sync(int const addr)
{
    assert(d_constEvalEnabled && "Cannot sync when constant evaluation is disabled");

    bool const valueKnown = not d_memory.valueUnknown(addr);
    bool const synced     = d_memory.isSync(addr);
    if (valueKnown && !synced)
        runtimeSetToValue(addr, d_memory.value(addr));
}

int Compiler::wrapValue(int val)
{
    val %= (MAX_INT + 1);
    if (val < 0)
        val += MAX_INT + 1;

    return val;
}

void Compiler::constEvalSetToValue(int const addr, int const val)
{
    int const newVal = wrapValue(val);
    d_memory.setSync(addr, false);
    d_memory.value(addr) = newVal;
}

void Compiler::runtimeSetToValue(int const addr, int const val)
{
    int newVal = wrapValue(val);
    d_codeBuffer << d_bfGen.setToValue(addr, newVal);
    d_memory.value(addr) = newVal;
    d_memory.setSync(addr, true);
}

void Compiler::runtimeAssign(int const lhs, int const rhs)
{
    d_codeBuffer << d_bfGen.assign(lhs, rhs);
    d_memory.setValueUnknown(lhs);
}
    
int Compiler::assign(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void expression in assignment.");
    
    int const leftSize = d_memory.sizeOf(lhs);
    int const rightSize = d_memory.sizeOf(rhs);
    
    if (leftSize > 1 && rightSize == 1)
    {
        // Fill array with value
        if (d_constEvalEnabled && !d_memory.valueUnknown(rhs))
        {
            for (int i = 0; i != leftSize; ++i)
                constEvalSetToValue(lhs + i, d_memory.value(rhs));
        }
        else
        {
            for (int i = 0; i != leftSize; ++i)
                runtimeAssign(lhs + i, rhs);
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
                    constEvalSetToValue(lhs + i, d_memory.value(rhs + i));
                else
                    runtimeAssign(lhs + i, rhs + i);
            }
        }
        else
        {
            for (int i = 0; i != leftSize; ++i)
                runtimeAssign(lhs + i, rhs + i);
        }
    }
    else if (leftSize == 1 && !d_memory.type(rhs).isStructType())
    {
        if (d_constEvalEnabled && !d_memory.valueUnknown(rhs))
            constEvalSetToValue(lhs, d_memory.value(rhs));
        else
            runtimeAssign(lhs, rhs);
    }
    else
    {
        compilerErrorIf(true, "Cannot assign variables of unequal sizes (sizeof(lhs) = ", leftSize,
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
    compilerErrorIf(addr < 0, "Unknown variable \"", expr[0], "\".");

    return fetchFieldImpl(expr, addr, 0);
}

int Compiler::fetchFieldImpl(std::vector<std::string> const &expr, int const baseAddr, size_t const baseIdx)
{
    TypeSystem::Type type = d_memory.type(baseAddr);
    compilerErrorIf(!type.isStructType(), "Type \"", type.name(), "\" is not a structure.");
    
    auto def = type.definition();
    for (auto const &f: def.fields())
    {
        if (f.name == expr[baseIdx + 1])
        {
            if (baseIdx + 2 == expr.size())
                return (baseAddr + f.offset);
            else
                return fetchFieldImpl(expr, baseAddr + f.offset, baseIdx + 1);
        }
    }

    compilerError("Structure \"", type.name(), "\" does not contain field \"", expr[baseIdx + 1], "\".");
    return -1;
}

int Compiler::arrayFromSize(int const sz, Instruction const &fill)
{
    compilerErrorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    return assign(allocateTemp(sz), fill());
}

int Compiler::arrayFromList(std::vector<Instruction> const &list)
{
    int const sz = list.size();

    compilerErrorIf(sz > MAX_ARRAY_SIZE,
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

    compilerErrorIf(sz > MAX_ARRAY_SIZE,
            "Maximum array size (", MAX_ARRAY_SIZE, ") exceeded (got ", sz, ").");

    int const start = allocateTemp(sz);
    for (int idx = 0; idx != sz; ++idx)
    {
        constEvalSetToValue(start + idx, str[idx]);
        if (!d_constEvalEnabled)
            runtimeSetToValue(start + idx, str[idx]);
    }

    return start;
}

int Compiler::anonymousStructObject(std::string const name, std::vector<Instruction> const &values)
{
    TypeSystem::Type type(name);
    compilerErrorIf(!type.defined(), "Unknown (struct) type \"", name, "\".");

    auto const def = type.definition();
    compilerErrorIf(values.size() > def.fields().size(),
            "Too many field-initializers provided to struct \"", name, "\": ",
            "expects ", def.fields().size(), ", got ", values.size(), ".");

    int const addr = allocateTemp(type);
    for (size_t i = 0; i != values.size(); ++i)
    {
        auto const &field = def.fields()[i];
        
        int val = values[i]();
        TypeSystem::Type valType = d_memory.type(val);
        TypeSystem::Type fieldType = field.type;
        
        compilerErrorIf(!(valType == fieldType),
                "Type mismatch in initialization of \"", name , ".", field.name, "\".");

        assign(addr + field.offset, val);
    }

    return addr;
}


int Compiler::fetchElement(AddressOrInstruction const &arr, AddressOrInstruction const &index)
{
    int const indexValue = d_memory.value(index);
    int const sz = d_memory.sizeOf(arr);
    compilerWarningIf(d_boundsCheckingEnabled && indexValue >= sz,
              "Array index (", indexValue, ") out of bounds: sizeof(",
              d_memory.identifier(arr), ") = ", sz, ".");

    if (d_constEvalEnabled && !d_memory.valueUnknown(index))
    {
        return arr + d_memory.value(index);
    }
    else
    {
        if (d_constEvalEnabled)
        {
            sync(index);
            for (int i = 0; i != sz; ++i)
                sync(arr + i);
        }
        
        int const ret = allocateTemp();
        d_codeBuffer << d_bfGen.fetchElement(arr, sz, index, ret);
        d_memory.setValueUnknown(ret);
        return ret;
    }
}

int Compiler::assignElement(AddressOrInstruction const &arr, AddressOrInstruction const &index, AddressOrInstruction const &rhs)
{
    int const indexValue = d_memory.value(index);
    int const sz = d_memory.sizeOf(arr);
    compilerWarningIf(d_boundsCheckingEnabled && indexValue >= sz,
              "Array index (", indexValue, ") out of bounds: sizeof(",
              d_memory.identifier(arr), ") = ", sz, ".");
    
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
        sync(rhs);
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
            for (int i = 0; i != sz; ++i)
                sync(arr + i);
        }
        
        d_codeBuffer << d_bfGen.assignElement(arr, sz, index, rhs);
        for (int i = 0; i != sz; ++i)
            d_memory.setValueUnknown(arr + i);

        // Attention: can't return the address of the modified cell, so we return the
        // address of the known RHS-cell.
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

int Compiler::scanCell()
{
    int const addr = allocateTemp();
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

int Compiler::randomCell()
{
    static bool warned = false;
    if (!d_randomExtensionEnabled && !warned)
    {
        compilerWarning("Random number generation is implemented using the non-standard 'Random Brainf*ck' extension (https://esolangs.org/wiki/Random_Brainfuck). Your interpreter must support the '?'-symbol.\nThis warning can be supressed with the --random flag.");
        warned = true;
    }
    
    int const addr = allocateTemp();
    d_codeBuffer << d_bfGen.random(addr);
    d_memory.setValueUnknown(addr);
    return addr;
}

int Compiler::preIncrement(AddressOrInstruction const &target)
{
    compilerErrorIf(target < 0, "Cannot increment void-expression.");
    
    auto bf   = [&, this](){
                    d_codeBuffer << d_bfGen.incr(target);
                };
    auto func = [](int x){ return ++x; };
    
    return eval<0b0>(bf, func, target, target);
}

int Compiler::postIncrement(AddressOrInstruction const &target)
{
    compilerErrorIf(target < 0, "Cannot increment void-expression.");

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
    compilerErrorIf(target < 0, "Cannot decrement void-expression.");

    auto bf   = [&, this](){
                    d_codeBuffer << d_bfGen.decr(target);
                };
    auto func = [](int x){ return --x; };
    
    return eval<0b0>(bf, func, target, target);
}


int Compiler::postDecrement(AddressOrInstruction const &target)
{
    compilerErrorIf(target < 0, "Cannot decrement void-expression.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in addition.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in subtraction.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");
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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in multiplication.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.multiply(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x * y;
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
    
}

int Compiler::power(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");

    int const ret = allocateTemp();
    auto bf = [&, this](){
                  d_codeBuffer << d_bfGen.power(lhs, rhs, ret);
              };

    auto func = [](int x, int y){
                    return std::pow(x, y);
                };

    return eval<0b00>(bf, func, ret, lhs, rhs);
}

int Compiler::powerBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");

    auto bf = [&, this](){
                  d_codeBuffer << d_bfGen.powerBy(lhs, rhs);
              };

    auto func = [](int x, int y){
                    return std::pow(x, y);
                };

    return eval<0b00>(bf, func, lhs, lhs, rhs);
}


int Compiler::divide(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in division.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in modulo-operation.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in modulo-operation.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in divmod-operation.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in moddiv-operation.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

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
    compilerErrorIf(lhs < 0 || rhs < 0, "Use of void-expression in comparison.");

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
    compilerErrorIf(arg < 0, "Use of void-expression in not-operation.");

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
    compilerErrorIf(lhs < 0,  "Use of void-expression in and-operation.");
    compilerErrorIf(d_memory.value(lhs) && rhs < 0, "Use of void-expression in and-operation.");
    
    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.logicalAnd(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x && y;
                };

    // RHS will be evaluated, even if it won't be at runtime due to short circuiting
    // This will result in a warning if it contains an out-of bounds index. Suppress this
    // warning by disabling this check.

    disableBoundChecking();
    int const result = eval<0b00>(bf, func, ret, lhs, rhs);
    enableBoundChecking();
    return result;
}

int Compiler::logicalOr(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
    compilerErrorIf(lhs < 0,  "Use of void-expression in or-operation.");
    compilerErrorIf(d_memory.value(lhs) && rhs < 0, "Use of void-expression in or-operation.");

    int const ret = allocateTemp();
    auto bf  = [&, this](){
                   d_codeBuffer << d_bfGen.logicalOr(lhs, rhs, ret);
               };

    auto func = [](int x, int y){
                    return x || y;
                };

    // RHS will be evaluated, even if it won't be at runtime due to short circuiting
    // This will result in a warning if it contains an out-of bounds index. Suppress this
    // warning by disabling this check.
    
    disableBoundChecking(); 
    int const result = eval<0b00>(bf, func, ret, lhs, rhs);
    enableBoundChecking();
    return result;
}


int Compiler::mergeInstructions(Instruction const &instr1, Instruction const &instr2)
{
    instr1();
    instr2();

    return -1;
}

void Compiler::enterScope(Scope::Type const type)
{
    d_scope.push(type);
    allocateBCRFlags(type != Scope::Type::If);
}

void Compiler::enterScope(std::string const &name)
{
    d_scope.push(name);
    allocateBCRFlags(true);
}

void Compiler::exitScope(std::string const &name)
{
    if (name.empty())
    {
        auto const outOfScope = d_scope.pop();
        std::string const &outOfScopeString = outOfScope.first;
        //        Scope::Type outOfScopeType = outOfScope.second; 
        
        d_memory.freeLocals(outOfScopeString);

        if (d_bcrEnabled)
        {
            auto const it = d_bcrMap.find(outOfScopeString);
            assert(it != d_bcrMap.end() && "Flag not found for this scope");
            d_bcrMap.erase(it);
        }
    }
    else
    {
        d_scope.popFunction(name);
        // memory cleanup performed by ::call()

        if (d_bcrEnabled)
        {
            auto const it = d_bcrMap.find(name);
            assert(it != d_bcrMap.end() && "Flag not found for this scope");
            d_bcrMap.erase(it);
        }
    }
}

void Compiler::allocateBCRFlags(bool const alloc)
{
    if (!d_bcrEnabled)
        return;
    
    int breakFlag = -1;
    int continueFlag = -1;

    if (alloc)
    {
        breakFlag = allocate("__break_flag", TypeSystem::Type(1));
        continueFlag = allocate("__continue_flag", TypeSystem::Type(1));
        
        if (d_constEvalEnabled)
        {
            constEvalSetToValue(breakFlag, 1);
            constEvalSetToValue(continueFlag, 1);
        }
        else
        {
            runtimeSetToValue(breakFlag, 1);
            runtimeSetToValue(continueFlag, 1);
        }
    }
    else
    {
        std::string const &enclosingScope = d_scope.enclosing();
        assert(!enclosingScope.empty() && "calling allocateBCRFlags(false) without being in a subscope");

        
        auto const it = d_bcrMap.find(enclosingScope);
        assert(it != d_bcrMap.end() && "enclosing scope not present in bcr-map");

        breakFlag = it->second.first;
        continueFlag = it->second.second;
    }

    assert(breakFlag != -1 && "break-flag-address not assigned");
    assert(continueFlag != -1 && "continue-flag-address not assigned");
    
    auto const result = d_bcrMap.insert({d_scope.current(), {breakFlag, continueFlag}});
    assert(result.second && "flags already present for this scope");
}

int Compiler::getCurrentBreakFlag() const
{
    assert(d_bcrEnabled && "calling getCurrentBreakFlag() with --no-bcr");
    
    auto const it = d_bcrMap.find(d_scope.current());
    assert(it != d_bcrMap.end() && "current scope not present in bcr-map");

    return it->second.first;
}

int Compiler::getCurrentContinueFlag() const
{
    assert(d_bcrEnabled && "calling getCurrentContinueFlag() with --no-bcr");
    
    auto const it = d_bcrMap.find(d_scope.current());
    assert(it != d_bcrMap.end() && "current scope not present in bcr-map");

    return it->second.second;
}

int Compiler::ifStatement(Instruction const &condition, Instruction const &ifBody, Instruction const &elseBody, bool const scoped)
{
    int const conditionAddr = condition();
    compilerErrorIf(conditionAddr < 0, "Use of void-expression in if-condition.");

    if (d_constEvalEnabled && !d_memory.valueUnknown(conditionAddr))
    {
        if (scoped)
            enterScope(Scope::Type::If);
        
        (d_memory.value(conditionAddr) > 0 ? ifBody : elseBody)();
        
        if (scoped)
            exitScope();
        
        return -1;
    }

    // Runtime evaluation
    disableConstEval();
    
    int const ifFlag = allocateTemp();
    assign(ifFlag, conditionAddr);
    int const elseFlag = logicalNot(ifFlag);


    d_codeBuffer << d_bfGen.movePtr(ifFlag)
                 << "[";

    {
        if (scoped)
            enterScope(Scope::Type::If);

        ifBody();

        if (scoped)
            exitScope();
    }
    
    d_codeBuffer << d_bfGen.setToValue(ifFlag, 0)
                 << "]"
                 << d_bfGen.movePtr(elseFlag)
                 << "[";

    {
        if (scoped)
            enterScope(Scope::Type::If);
        
        elseBody();
        
        if (scoped)
            exitScope();
    }
    
    d_codeBuffer << d_bfGen.setToValue(elseFlag, 0)
                 << "]";


    if (d_bcrEnabled)
    {
        // If/else might have changed bcr-flags --> mark values unknown
        d_memory.setValueUnknown(getCurrentContinueFlag());
        for (auto const &pr: d_bcrMap)
        {
            if (pr.first.find(d_scope.function()) == 0)
            {
                int const breakFlag = pr.second.first;
                d_memory.setValueUnknown(breakFlag);
            }
        }
    }

    enableConstEval();
    
    return -1;
}

int Compiler::forStatement(Instruction const &init, Instruction const &condition,
                           Instruction const &increment, Instruction const &body)
{
    if (!d_constEvalEnabled)
        return forStatementRuntime(init, condition, increment, body);

    State state = save();
    enterScope(Scope::Type::For);
    init();
    int conditionAddr = condition();
    
    if (d_memory.valueUnknown(conditionAddr))
    {
        restore(std::move(state));
        return forStatementRuntime(init, condition, increment, body);
    }

    int count = 0;
    while (d_memory.value(conditionAddr))
    {
        body();
        resetContinueFlag();
        increment();
        conditionAddr = d_bcrEnabled ? logicalAnd(condition, getCurrentBreakFlag()) : condition();
        d_returnExistingAddressOnAlloc = true;

        if (d_memory.valueUnknown(conditionAddr) || count++ > MAX_LOOP_UNROLL_ITERATIONS)
        {
            restore(std::move(state));
            return forStatementRuntime(init, condition, increment, body);
        }

    }    

    d_returnExistingAddressOnAlloc = false;
    exitScope();
    
    return -1;
}

int Compiler::forStatementRuntime(Instruction const &init, Instruction const &condition,
                                  Instruction const &increment, Instruction const &body)
{
    int const flag = allocateTemp();
    enterScope(Scope::Type::For);
    disableConstEval();

    init();
    int conditionAddr = condition();
    compilerErrorIf(conditionAddr < 0, "Use of void-expression in for-condition.");

    d_codeBuffer << d_bfGen.assign(flag, conditionAddr);
    d_codeBuffer << "[";

    body();
    resetContinueFlag();
    increment();
    conditionAddr = d_bcrEnabled ? logicalAnd(condition, getCurrentBreakFlag()) : condition();
                               
    d_codeBuffer << d_bfGen.assign(flag, conditionAddr)
                 << "]";

    exitScope();
    enableConstEval();
    return -1;
}

int Compiler::forRangeStatement(std::string const &ident, Instruction const &array, Instruction const &body)
{
    State state = save();
    int const arrayAddr = array();
    int const nIter = d_memory.sizeOf(arrayAddr);
    if (nIter > MAX_LOOP_UNROLL_ITERATIONS)
    {
        restore(std::move(state));
        return forRangeStatementRuntime(ident, array, body);
    }

    enterScope(Scope::Type::For);
    int const elementAddr = declareVariable(ident, TypeSystem::Type(1));
    for (int i = 0; i != nIter; ++i)
    {
        assign(elementAddr, arrayAddr + i);
        body();
        resetContinueFlag();
        d_returnExistingAddressOnAlloc = true;
    }    

    d_returnExistingAddressOnAlloc = false;
    exitScope();
    
    return -1;

}

int Compiler::forRangeStatementRuntime(std::string const &ident, Instruction const &array, Instruction const &body)
{
    int const tmp = allocateTempBlock(3);
    int const iterator = tmp + 0;
    int const flag = tmp + 1;
    int const finalIdx = tmp + 2;
    
    disableConstEval();
    int const arrayAddr = array();
    int const nIter = d_memory.sizeOf(arrayAddr);
    enterScope(Scope::Type::For);
    int const elementAddr = declareVariable(ident, TypeSystem::Type(1));
    compilerErrorIf(elementAddr < 0 || arrayAddr < 0, "Use of void-expression in for-initialization.");

    d_codeBuffer << d_bfGen.setToValue(iterator, 0)
                 << d_bfGen.setToValue(finalIdx, nIter)
                 << d_bfGen.setToValue(flag, 1)
                 << "["
                 <<    d_bfGen.fetchElement(arrayAddr, nIter, iterator, elementAddr);

    body();
    resetContinueFlag();
    int finalElementCheck = notEqual(iterator, finalIdx);
    int conditionAddr = d_bcrEnabled ? logicalAnd(finalElementCheck, getCurrentBreakFlag()) : finalElementCheck;
    
    d_codeBuffer <<    d_bfGen.incr(iterator)
                 <<    d_bfGen.assign(flag, conditionAddr)
                 << "]";
    
    exitScope();
    enableConstEval();
    return -1;
}

int Compiler::whileStatement(Instruction const &condition, Instruction const &body)
{
    if (!d_constEvalEnabled)
        return whileStatementRuntime(condition, body);

    State state = save();
    enterScope(Scope::Type::While);
    
    int conditionAddr = condition();
    if (d_memory.valueUnknown(conditionAddr))
    {
        restore(std::move(state));
        return whileStatementRuntime(condition, body);
    }

    int count = 0;
    while (d_memory.value(conditionAddr))
    {
        body();
        resetContinueFlag();
        conditionAddr = d_bcrEnabled ? logicalAnd(condition, getCurrentBreakFlag()) : condition();
        d_returnExistingAddressOnAlloc = true;
        
        if (d_memory.valueUnknown(conditionAddr) || (count++ > MAX_LOOP_UNROLL_ITERATIONS))
        {
            restore(std::move(state));
            return whileStatementRuntime(condition, body);
        }
    }
    
    d_returnExistingAddressOnAlloc = false;
    exitScope();
    return -1;    
}

int Compiler::whileStatementRuntime(Instruction const &condition, Instruction const &body)
{
    int const flag = condition();
    compilerErrorIf(flag < 0, "Use of void-expression in while-condition.");

    enterScope(Scope::Type::While);
    disableConstEval();
    
    d_codeBuffer << d_bfGen.movePtr(flag)
                 << "[";
    body();
    resetContinueFlag();
    int conditionAddr = d_bcrEnabled ? logicalAnd(condition, getCurrentBreakFlag()) : condition();
    
    d_codeBuffer << d_bfGen.assign(flag, conditionAddr)
                 << "]";

    exitScope();
    enableConstEval();
    return -1;
}

int Compiler::switchStatement(Instruction const &compareExpr,
                              std::vector<std::pair<Instruction, Instruction>> const &cases,
                              Instruction const &defaultCase)
{
    std::function<Instruction(size_t const)> ifElseLadder;

    ifElseLadder =
        [&, this](size_t const idx) -> Instruction
        {
            if (idx == cases.size() - 1)
            {
                return instruction<&Compiler::ifStatement>
                    (instruction<&Compiler::equal>(compareExpr, cases[idx].first),
                     cases[idx].second, defaultCase, true);
            }
                
            return instruction<&Compiler::ifStatement>
                (instruction<&Compiler::equal>(compareExpr, cases[idx].first),
                 cases[idx].second, ifElseLadder(idx + 1), true);
        };
    
    return ifElseLadder(0)();
}

int Compiler::breakStatement()
{
    compilerErrorIf(!d_bcrEnabled, "break-statement not supported when compiling with --no-bcr");
    
    int const flag = getCurrentBreakFlag();
    if (d_constEvalEnabled)
    {
        constEvalSetToValue(flag, 0);
    }
    else
    {
        runtimeSetToValue(flag, 0);
    }
    
    return -1;
}

int Compiler::continueStatement()
{
    compilerErrorIf(!d_bcrEnabled, "continue-statement not supported when compiling with --no-bcr");
    
    int const flag = getCurrentContinueFlag();
    if (d_constEvalEnabled)
        constEvalSetToValue(flag, 0);
    else
        runtimeSetToValue(flag, 0);
    
    return -1;
}

void Compiler::resetContinueFlag()
{
    if (!d_bcrEnabled)
        return;
    
    int const flag = getCurrentContinueFlag();
    if (d_constEvalEnabled)
        constEvalSetToValue(flag, 1);
    else
        runtimeSetToValue(flag, 1);
}

int Compiler::returnStatement()
{
    compilerErrorIf(!d_bcrEnabled, "return-statement not supported when compiling with --no-bcr");
    
    std::string const func = d_scope.function();
    for (auto const &pr: d_bcrMap)
    {
        if (pr.first.find(func) == 0)
        {
            int const breakFlag = pr.second.first;
            if (d_constEvalEnabled)
                constEvalSetToValue(breakFlag, 0);
            else
                runtimeSetToValue(breakFlag, 0);
        }
    }

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
