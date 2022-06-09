#ifndef BFXFUNCTION_H
#define BFXFUNCTION_H

#include <functional>
#include <vector>
#include <string>
#include <variant>
#include <cassert>
#include <deque>
#include "typesystem.h"

// namespace BFX ?????

using Instruction = std::function<int()>;
using VariableDeclaration = std::pair<std::string,
                                      std::variant<int, std::string>>;

class BFXFunction
{
public:   
    enum class ParameterType
        {
         Value,
         Reference
        };

    using Parameter = std::pair<std::string, ParameterType>;
    
private:
    
    std::string            d_name;
    Instruction            d_body;
    std::vector<Parameter> d_params;
    std::string            d_returnVar;

public:
    BFXFunction() = default;
    BFXFunction(std::string name, std::vector<Parameter> const &params):
        d_name(name),
        d_params(params)
    {}

    BFXFunction &setBody(Instruction const &body)
    {
        d_body = body;
        return *this;
    }
    
    BFXFunction &setReturnVariable(std::string const &ident)
    {
        d_returnVar = ident;
        return *this;
    }

    Instruction const &body() const
    {
        return d_body;
    }

    std::vector<Parameter> const &params() const
    {
        return d_params;
    }

    std::string const &returnVariable() const
    {
        return d_returnVar;
    }

    std::string const &name() const
    {
        return d_name;
    }

    bool isVoid() const
    {
        return d_returnVar.empty();
    }
};

class Scope
{
    template <typename T>
    using StackType = std::deque<T>;

    StackType<std::pair<std::string, StackType<int>>> d_stack;
        
public:
    bool empty() const;
    std::string function() const;
    std::string current() const;
    std::string enclosing() const;
    bool containsFunction(std::string const &name) const;
    void push(std::string const &name = "");
    std::string popFunction(std::string const &name);
    std::string pop();
};
    
class AddressOrInstruction
{
    enum class Kind
        {
         ADDRESS,
         INSTRUCTION
        };
    
    mutable Kind d_kind;
    mutable int d_addr;
    Instruction const d_instr;
    
public:
    AddressOrInstruction(int addr):
        d_kind(Kind::ADDRESS),
        d_addr(addr)
    {}
    
    AddressOrInstruction(Instruction const &instr):
        d_kind(Kind::INSTRUCTION),
        d_instr(instr)
    {}

    operator int() const
    {
        if (d_kind == Kind::INSTRUCTION)
        {
            // Lazy evaluation
            d_addr = d_instr();
            d_kind = Kind::ADDRESS;
        }
        
        return d_addr;
    }
};


#endif 
