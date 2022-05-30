#ifndef BFGENERATOR_H
#define BFGENERATOR_H

#include <iostream>
#include <functional>

class BFGenerator
{
    size_t                  d_pointer;
    std::function<int()>    f_getTemp;
    std::function<int(int)> f_getTempBlock;
    std::function<int()>    f_getMemSize;
    
public:
    size_t getPointerIndex() const
    {
        return d_pointer;
    }
    
    template <typename GetTemp>
    void setTempRequestFn(GetTemp &&getTemp)
    {
        f_getTemp = std::forward<GetTemp>(getTemp);
    }
    
    template <typename GetTempBlock>
    void setTempBlockRequestFn(GetTempBlock &&getTempBlock)
    {
        f_getTempBlock = std::forward<GetTempBlock>(getTempBlock);
    }
    
    template <typename GetMemSize>
    void setMemSizeRequestFn(GetMemSize &&getMemSize)
    {
        f_getMemSize = std::forward<GetMemSize>(getMemSize);
    }
    
    std::string movePtr(int const addr);
    std::string scan(int const addr);
    std::string print(int const addr);
    std::string random(int const addr);
    std::string fetchElement(int const arrStart, int const arrSize, int const index, int const ret);
    std::string setToValue(int const addr, int const val);
    std::string setToValue(int const start, int const val, size_t const n);
    std::string setToValuePlus(int const addr, int const val);
    std::string setToValuePlus(int const addr, int const val, size_t const n);
    std::string assign(int const lhs, int const rhs);
    std::string assignElement(int const arrStart, int const arrSize, int const index, int const val);
    std::string addTo(int const target, int const rhs);
    std::string addConst(int const target, int const amount);
    std::string incr(int const target);
    std::string decr(int const target);
    std::string safeDecr(int const target, int const underflow);
    std::string subtractFrom(int const target, int const rhs);
    std::string multiply(int const lhs, int const rhs, int const result);
    std::string multiplyBy(int const target, int const rhs);
    std::string divmod(int const num, int const denom, int const divResult, int const modResult);
    std::string equal(int const lhs, int const rhs, int const result);
    std::string notEqual(int const lhs, int const rhs, int const result);
    std::string greater(int const lhs, int const rhs, int const result);
    std::string less(int const lhs, int const rhs, int const result);
    std::string greaterOrEqual(int const lhs, int const rhs, int const result);
    std::string lessOrEqual(int const lhs, int const rhs, int const result);
    std::string logicalNot(int const operand);
    std::string logicalNot(int const operand, int const result);
    std::string logicalAnd(int const lhs, int const rhs, int const result);
    std::string logicalAnd(int const lhs, int const rhs);
    std::string logicalOr(int const lhs, int const rhs, int const result);
    std::string logicalOr(int const lhs, int const rhs);
    
private:
    
    template <typename ... Rest>
    void validateAddr__(std::string const &function, int first, Rest&& ... rest) const
    {
        if (first < 0 || ((rest < 0) || ...))
        {
            std::cerr << "Fatal internal error in call to " << function
                      << ": negative address.\n\n"
                      << "Compilation terminated\n";
            std::exit(1);
        }

        int const sz = f_getMemSize();
        if (first > sz || (((int)rest >= sz) || ...))
        {
            std::cerr << "Fatal internal error in call to " << function
                      << ": address out of bounds.\n\n"
                      << "Compilation terminated\n";

            std::exit(1);
        }
    }

};

#endif //BFGENERATOR_H
