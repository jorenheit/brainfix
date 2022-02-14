#ifndef BFXFUNCTION_H
#define BFXFUNCTION_H

#include <functional>
#include <vector>
#include <string>
#include <variant>
#include <cassert>

using Instruction = std::function<int()>;

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
