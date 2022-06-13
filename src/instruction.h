#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <functional>

using Instruction = std::function<int()>;

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
