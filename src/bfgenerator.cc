#include "bfgenerator.ih"

std::string BFGenerator::setToValue(int const addr, int const val)
{
    validateAddr(addr, val);

    std::ostringstream ops;
    ops << movePtr(addr)        // go to address
        << "[-]"                   // reset cell to 0
        << std::string(val, '+');  // increment to value

    return ops.str();
}

std::string BFGenerator::setToValuePlus(int const addr, int const val)
{
    validateAddr(addr, val);

    std::ostringstream ops;
    ops << movePtr(addr)        // go to address
        << "[+]"                   // reset cell to 0
        << std::string(val, '+');  // increment to value

    return ops.str();
}

std::string BFGenerator::setToValue(int const start, int const val, size_t const n)
{
    validateAddr(start, val);
    
    std::ostringstream ops;
    for (size_t i = 0; i != n; ++i)
        ops << setToValue(start + i, val);

    return ops.str();
}

std::string BFGenerator::setToValuePlus(int const addr, int const val, size_t const n)
{
    validateAddr(addr, val);
    
    std::ostringstream ops;
    for (size_t i = 0; i != n; ++i)
        ops << setToValuePlus(addr + i, val);

    return ops.str();
}
std::string BFGenerator::scan(int const addr)
{
    std::ostringstream ops;
    ops << movePtr(addr)
        << ',';

    return ops.str();
}

std::string BFGenerator::print(int const addr)
{
    std::ostringstream ops;
    ops << movePtr(addr)
        << '.';

    return ops.str();
}

std::string BFGenerator::random(int const addr)
{
    std::ostringstream ops;
    ops << movePtr(addr)
        << '?';

    return ops.str();
}

std::string BFGenerator::assign(int const lhs, int const rhs)
{
    validateAddr(lhs, rhs);
    
    int const tmp = f_getTemp();
        
    std::ostringstream ops;
    ops    << setToValue(lhs, 0)
           << setToValue(tmp, 0)

        // Move contents of RHS to both LHS and TMP (backup)
           << movePtr(rhs)         
           << "["
           <<     incr(lhs)
           <<     incr(tmp)
           <<     decr(rhs)
           << "]"

        // Restore RHS by moving TMP back into it
           << movePtr(tmp)
           << "["
           <<     incr(rhs)
           <<     decr(tmp)
           << "]"

        // Leave pointer at lhs
           << movePtr(lhs);

    return ops.str();
}

std::string BFGenerator::movePtr(int const addr)
{
    validateAddr(addr);
    
    int const diff = (int)addr - (int)d_pointer;
    d_pointer = addr;
    return (diff >= 0) ? std::string(diff, '>') : std::string(-diff, '<');
}

std::string BFGenerator::addConst(int const target, int const amount)
{
    validateAddr(target);

    std::ostringstream ops;
    ops << movePtr(target)
        << ((amount >= 0) ? std::string(amount, '+') : std::string(-amount, '-'));

    return ops.str();
}

std::string BFGenerator::addTo(int const target, int const rhs)
{
    validateAddr(target, rhs);
    
    std::ostringstream ops;
    int const tmp = f_getTemp();
    ops    << assign(tmp, rhs)
           << "["
           <<     incr(target)
           <<     decr(tmp)
           << "]"
           << movePtr(target);
    
    return ops.str();
}

std::string BFGenerator::subtractFrom(int const target, int const rhs)
{
    validateAddr(target, rhs);
    
    int const tmp = f_getTemp();
    std::ostringstream ops;
    ops    << assign(tmp, rhs)
           << "["
           <<     decr(target)
           <<     decr(tmp)
           << "]"
           << movePtr(target);
    
    return ops.str();
}

std::string BFGenerator::incr(int const target)
{
    validateAddr(target);
    return movePtr(target) + "+";
}

std::string BFGenerator::decr(int const target)
{
    validateAddr(target);
    return movePtr(target) + "-";
}

std::string BFGenerator::safeDecr(int const target, int const underflowFlag)
{
    validateAddr(target, underflowFlag);

    std::ostringstream ops;
    ops << logicalNot(target, underflowFlag)
        << movePtr(target)
        << "-";

    return ops.str();
}

std::string BFGenerator::multiply(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    std::ostringstream ops;
    ops << assign(result, lhs)
        << multiplyBy(result, rhs);

    return ops.str();
}

std::string BFGenerator::power(int const base, int const pow, int const result)
{
    validateAddr(base, pow, result);

    int const tmp = f_getTemp();
    
    std::ostringstream ops;
    ops << setToValue(result, 1)
        << assign(tmp, pow)
        << "["
        <<     multiplyBy(result, base)
        <<     decr(tmp)
        << "]"
        << movePtr(result);

    return ops.str();
}

std::string BFGenerator::multiplyBy(int const target, int const factor)
{
    validateAddr(target, factor);

    int const tmp = f_getTempBlock(2);
    int const targetCopy = tmp + 0;
    int const count      = tmp + 1;

    std::ostringstream ops;
    ops << assign(targetCopy, target)
        << setToValue(target, 0)
        << assign(count, factor)
        << "["
        <<     addTo(target, targetCopy)
        <<     decr(count)
        << "]"
        << movePtr(target);
    
    return ops.str();
}

std::string BFGenerator::logicalNot(int const addr, int const result)
{
    validateAddr(addr, result);
    
    int const tmp = f_getTemp();
    std::ostringstream ops;
    
    ops    << setToValue(result, 1)
           << assign(tmp, addr)
           << "["
           <<     setToValue(result, 0)
           <<     setToValue(tmp, 0)
           << "]"
           << movePtr(result);

    return ops.str();
}

std::string BFGenerator::logicalNot(int const addr)
{
    validateAddr(addr);
    
    int flag = f_getTemp();
    
    std::ostringstream ops;
    ops    << setToValue(flag, 1)
           << movePtr(addr)
           << "["
           <<     setToValue(flag, 0)
           <<     setToValue(addr, 0)
           << "]"
           << movePtr(flag)
           << "["
           <<     setToValue(addr, 1)
           <<     setToValue(flag, 0)
           << "]"
           << movePtr(addr);

    return ops.str();
}

std::string BFGenerator::logicalAnd(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp = f_getTempBlock(2);
    int const x = tmp + 0;
    int const y = tmp + 1;
    
    std::ostringstream ops;
    ops    << setToValue(result, 0)
           << assign(y, rhs)
           << assign(x, lhs)
           << "["
           <<     movePtr(y)
           <<     "["
           <<         setToValue(result, 1)
           <<         setToValue(y, 0)
           <<     "]"
           <<     setToValue(x, 0)
           << "]"
           << movePtr(result);

    return ops.str();
}

std::string BFGenerator::logicalAnd(int const lhs, int const rhs)
{
    validateAddr(lhs, rhs);

    int const result = f_getTemp();
    
    std::ostringstream ops;
    ops    << logicalAnd(lhs, rhs, result)
           << assign(lhs, result);
    
    return ops.str();
}
    

std::string BFGenerator::logicalOr(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp = f_getTempBlock(2);
    int const x = tmp + 0;
    int const y = tmp + 1;

    std::ostringstream ops;
    ops    << setToValue(result, 0)
           << assign(x, lhs)
           << "["
           <<     setToValue(result, 1)
           <<     setToValue(x, 0)
           << "]"
           << assign(y, rhs)
           << "["
           <<     setToValue(result, 1)
           <<     setToValue(y, 0)
           << "]"
           << movePtr(result);

    return ops.str();
}

std::string BFGenerator::logicalOr(int const lhs, int const rhs)
{
    validateAddr(lhs, rhs);

    int const result = f_getTemp();
    
    std::ostringstream ops;
    ops    << logicalOr(lhs, rhs, result)
           << assign(lhs, result);
    
    return ops.str();
}
    
std::string BFGenerator::equal(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp = f_getTempBlock(6);
    int const x = tmp + 0;
    int const y = tmp + 1;
    int const underflow1 = tmp + 2;
    int const underflow2 = tmp + 3;
    int const underflow3 = tmp + 4;
    int const yBigger = tmp + 5;

    std::ostringstream ops;
    ops << setToValue(result, 1)
        << setToValue(underflow1, 0)
        << assign(y, rhs)
        << assign(x, lhs)
        << "["
        <<     safeDecr(y, underflow2)
        <<     logicalOr(underflow1, underflow2)
        <<     movePtr(underflow2)
        <<     "["
        <<         setToValuePlus(y, 0)
        <<         setToValue(underflow2, 0)
        <<     "]"
        <<     decr(x)
        << "]"
        << assign(underflow3, underflow1)
        << "["  // if underflow -> y was smaller than x so not equal
        <<     setToValue(result, 0)
        <<     setToValuePlus(y, 1)
        <<     setToValue(underflow3, 0)
        << "]"
        << logicalNot(underflow1)
        << logicalAnd(y, underflow1, yBigger)
        << "["  // if y > 0 and did not underflow -> y was bigger than x so not equal
        <<     setToValue(result, 0) 
        <<     setToValue(yBigger, 0)
        << "]"
        << movePtr(result);

    return ops.str();
}

std::string BFGenerator::notEqual(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);
    
    int const isEqual = f_getTemp();
    std::ostringstream ops;
    ops << equal(lhs, rhs, isEqual)
        << logicalNot(isEqual, result);

    return ops.str();
}

std::string BFGenerator::greater(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp  = f_getTempBlock(3);
    int const x    = tmp + 0;
    int const y    = tmp + 1;
    int const underflow = tmp + 2;

    std::ostringstream ops;
    ops << setToValue(result, 0)
        << setToValue(underflow, 0)
        << assign(y, rhs)
        << assign(x, lhs)
        << "["
        <<     safeDecr(y, underflow)
        <<     logicalOr(result, underflow)
        <<     movePtr(underflow)
        <<     "["
        <<         setToValuePlus(y, 0)
        <<         setToValue(underflow, 0)
        <<     "]"
        <<     decr(x)
        << "]"
        << movePtr(result);

    return ops.str();
}

std::string BFGenerator::less(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);
    
    return greater(rhs, lhs, result); // reverse arguments
}

std::string BFGenerator::greaterOrEqual(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);

    int const tmp       = f_getTempBlock(2);
    int const isEqual   = tmp + 0;
    int const isGreater = tmp + 1;

    std::ostringstream ops;
    ops    << equal(lhs, rhs, isEqual)
           << greater(lhs, rhs, isGreater)
           << logicalOr(isEqual, isGreater, result);

    return ops.str();
}

std::string BFGenerator::lessOrEqual(int const lhs, int const rhs, int const result)
{
    validateAddr(lhs, rhs, result);
    
    return greaterOrEqual(rhs, lhs, result); // reverse arguments
}

std::string BFGenerator::fetchElement(int const arrStart, int const arrSize, int const index, int const ret)
{
    // Algorithms to move an unknown amount to the left and right.
    // Assumes the pointer points to a cell containing the amount
    // it needs to be shifted and a copy of this amount adjacent to it.
    // Also, neighboring cells must all be zeroed out.
               
    static std::string const dynamicMoveRight = "[>[->+<]<[->+<]>-]";
    static std::string const dynamicMoveLeft  = "[<[-<+>]>[-<+>]<-]<";

    // Allocate a buffer with 3 additional cells:
    // 1. to keep a copy of the index
    // 2. to store a temporary necessary for copying
    // 3. to prevent overflow on off-by-one errors

    int const bufSize = arrSize + 3;
    int const buf = f_getTempBlock(bufSize);
    int const dist = buf - arrStart;

    std::string const arr2buf(std::abs(dist), (dist > 0 ? '>' : '<'));
    std::string const buf2arr(std::abs(dist), (dist > 0 ? '<' : '>'));

    std::ostringstream ops;
    ops << assign(buf + 0, index)
        << assign(buf + 1, buf)
        << setToValue(buf + 2, 0, bufSize - 2)
        << movePtr(buf)
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
        << assign(ret, buf);

    return ops.str();
}

std::string BFGenerator::assignElement(int const arrStart, int const arrSize, int const index, int const val)
{
    static std::string const dynamicMoveRight = "[>>[->+<]<[->+<]<[->+<]>-]";
    static std::string const dynamicMoveLeft = "[[-<+>]<-]<";
               
    int const bufSize = arrSize + 3;
    int const buf     = f_getTempBlock(bufSize);
    int const dist    = buf - arrStart;

    std::string const arr2buf(std::abs(dist), (dist > 0 ? '>' : '<'));
    std::string const buf2arr(std::abs(dist), (dist > 0 ? '<' : '>'));

    std::ostringstream ops;
    ops << assign(buf, index)
        << assign(buf + 1, buf)
        << assign(buf + 2, val)
        << setToValue(buf + 3, 0, bufSize - 3)
        << movePtr(buf)
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
    
    return ops.str();
}

std::string BFGenerator::divmod(int const num, int const denom, int const divResult, int const modResult)
{
    int const tmp = f_getTempBlock(4);
    int const tmp_loopflag  = tmp + 0;
    int const tmp_zeroflag  = tmp + 1;
    int const tmp_num       = tmp + 2;
    int const tmp_denom     = tmp + 3;
    
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

    std::ostringstream ops;
    ops << setToValue(divResult, 0)             // 1
        << setToValue(modResult, 0)
        << assign(tmp_num, num)
        << assign(tmp_denom, denom)
        << setToValue(tmp_loopflag, 1)
        << logicalNot(denom, tmp_zeroflag)
        << "["                                      // 2
        <<     setToValue(tmp_loopflag, 0)
        <<     setToValue(divResult, 255)
        <<     setToValue(modResult, 255)
        <<     setToValue(tmp_zeroflag, 0)
        << "]"
        << logicalNot(num, tmp_zeroflag)
        << "["                                      // 3
        <<     setToValue(tmp_loopflag, 0)
        <<     setToValue(divResult, 0)
        <<     setToValue(modResult, 0)
        <<     setToValue(tmp_zeroflag, 0)
        << "]"
        << movePtr(tmp_loopflag)
        << "["                                      // 4
        <<     decr(tmp_num)
        <<     decr(tmp_denom)
        <<     incr(modResult)
        <<     logicalNot(tmp_denom, tmp_zeroflag)
        <<     "["
        <<         incr(divResult)
        <<         assign(tmp_denom, denom)
        <<         setToValue(modResult, 0)
        <<         setToValue(tmp_zeroflag, 0)
        <<     "]"
        <<     logicalNot(tmp_num, tmp_zeroflag)
        <<     "["
        <<         setToValue(tmp_loopflag, 0)
        <<         setToValue(tmp_zeroflag, 0)
        <<     "]"
        <<     movePtr(tmp_loopflag)
        << "]";

    return ops.str();
}
