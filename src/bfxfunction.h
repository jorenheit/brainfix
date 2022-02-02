#ifndef BFXFUNCTION_H
#define BFXFUNCTION_H

#include <functional>
#include <vector>
#include <string>
#include <variant>
#include <cassert>

using Instruction = std::function<int()>;
/*
class Instruction
{
    std::function<int()> d_func;
    mutable bool d_void{false};

public:
    Instruction() = default;
    
    Instruction(std::function<int()> const &f):
        d_func(f)
    {}

    int operator()() 
    {
        return d_func();
    }

    int operator()() const
    {
        return d_func();
    }
};
*/

class BFXFunction
{
    std::string d_name;
    Instruction d_body;
    std::vector<std::string> d_params;
    std::string d_returnVar;

public:
    static std::string const VOID;
    
    BFXFunction() = default;
    BFXFunction(std::string name, std::vector<std::string> const &params):
        d_name(name),
        d_params(params),
        d_returnVar(VOID)
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

    std::vector<std::string> const &params() const
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
        return d_returnVar == VOID;
    }
};
    
class AddressOrInstruction
{
    std::variant<int, Instruction> d_variant;
    mutable bool d_ready{false};
    mutable int d_value{-1};
    
public:
    AddressOrInstruction(int i):
        d_variant(i)
    {}
    
    AddressOrInstruction(Instruction const &f):
        d_variant(f)
    {}

    int get() const
    {
        if (!d_ready)
        {
            d_value = std::holds_alternative<int>(d_variant) ?
                std::get<int>(d_variant) :
                std::get<Instruction>(d_variant)();

            d_ready = true;
        }
        return d_value;
    }

    operator int() const
    {
        return get();
    }
};


#endif 
