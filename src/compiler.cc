#include "compiler.h"
#include <algorithm>

void Compiler::addFunction(BFXFunction bfxFunc, Instruction const &body)
{
	bfxFunc.setBody(body);
	auto result = d_functionMap.insert({bfxFunc.name(), bfxFunc});
	assert(result.second && "Redefinition of function not allowed");
}

void Compiler::addGlobals(std::vector<Instruction> const &variables)
{
	for (auto const &var: variables)
	{
		int addr = var(); // execute instruction
		d_memory.markAsGlobal(addr);
	}
}

void Compiler::addConstant(std::string const &ident, uint8_t num)
{
	auto result = d_constMap.insert({ident, num});
	assert(result.second && "Redefinition of constant not allowed");
}

uint8_t Compiler::compileTimeConstant(std::string const &ident) const
{
	assert(isCompileTimeConstant(ident) && "Not a constant");
	return d_constMap.at(ident);
}

bool Compiler::isCompileTimeConstant(std::string const &ident) const
{
	auto it = d_constMap.find(ident);
	return it != d_constMap.end();
}

int Compiler::allocateOrGet(std::string const &ident, uint8_t sz)
{
	std::string const scope = d_callStack.empty() ? "" : d_callStack.back();
	int addr = addressOf(ident);
	return (addr != -1) ? addr : d_memory.allocateLocalUnsafe(ident, scope, sz);
}

int Compiler::addressOf(std::string const &ident)
{
	std::string const scope = d_callStack.empty() ? "" : d_callStack.back();
	int addr = d_memory.findLocal(ident, scope);
	if (addr == -1)
	{
		// variable not defined in local scope -> look for globals
		addr = d_memory.findGlobal(ident);
	}

	return addr;
}

int Compiler::allocateTemp(uint8_t sz)
{
	int addr = d_memory.getTemp(d_callStack.back(), sz);
	return addr;
}

void Compiler::pushStack(int addr)
{
	d_memory.stack(addr);
	d_stack.push(addr);
}

int Compiler::popStack()
{
	int addr = d_stack.top();
	d_memory.unstack(addr);
	d_stack.pop();
	return addr;
}

void Compiler::freeTemps()
{
	d_memory.freeTemps(d_callStack.back());
}

void Compiler::freeLocals()
{
	if (d_callStack.back() != "main")
		d_memory.freeLocals(d_callStack.back());
}

std::string Compiler::bf_setToValue(int addr, int val)
{
	std::string ops;
	ops += bf_movePtr(addr);        // go to address
	ops += "[-]";                   // reset cell to 0
	ops += std::string(val, '+');   // increment to value

	return ops;
}

std::string Compiler::bf_setToValue(int start, int val, size_t n)
{
	std::string ops;
	for (size_t i = 0; i != n; ++i)
		ops += bf_setToValue(start + i, val);

	return ops;
}

std::string Compiler::bf_assign(int lhs, int rhs)
{
	assert(lhs >= 0 && rhs >= 0 &&
		   "Invalid address. Did you try to use the result of a void function?");
	
	int tmp = allocateTemp();

	std::string ops;
	ops += bf_movePtr(lhs) + "[-]"; // reset LHS
	ops += bf_movePtr(tmp) + "[-]"; // reset TMP

	// Move contents of RHS to both LHS and TMP (backup)
	ops += bf_movePtr(rhs);         
	ops += "[-";
	ops += bf_movePtr(lhs);
	ops += "+";
	ops += bf_movePtr(tmp);
	ops += "+";
	ops += bf_movePtr(rhs);
	ops += "]";

	// Restore RHS by moving TMP back into it
	ops += bf_movePtr(tmp);
	ops += "[-";
	ops += bf_movePtr(rhs);
	ops += "+";
	ops += bf_movePtr(tmp);
	ops += "]";

	// Leave pointer at lhs
	ops += bf_movePtr(lhs);

	return ops;
}

std::string Compiler::bf_assign(int dest, int src, size_t n)
{
	std::string result;
	for (size_t idx = 0; idx != n; ++idx)
		result += bf_assign(dest + idx, src + idx);

	return result;
}

std::string Compiler::bf_assign(std::vector<int> const &dest,
								std::vector<int> const &src)
{
	assert(dest.size() == src.size() &&
		   "Destination and source-vectors have different sizes.");

	std::string result;
	size_t const sz = dest.size();
	for (size_t idx = 0; idx != sz; ++idx)
		result += bf_assign(dest[idx], src[idx]);

	return result;
}

std::string Compiler::bf_movePtr(size_t addr)
{
	assert(addr >= 0 && "bf_movePtr: got invalid (negative) address");
	assert(addr < d_memory.size() && "bf_movePtr: pointer out of bounds during compilation.");

	int diff = (int)addr - (int)d_pointer;
	d_pointer = addr;
	return (diff >= 0) ? std::string(diff, '>')	: std::string(-diff, '<');
}

std::string Compiler::bf_addTo(int target, int rhs)
{
	std::string result;

	int tmp = allocateTemp();
	result += bf_assign(tmp, rhs);
	result += bf_movePtr(tmp);
	result += "[-";
	result += bf_movePtr(target);
	result += "+";
	result += bf_movePtr(tmp);
	result += "]";
	result += bf_movePtr(target);
	
	return result;
}

std::string Compiler::bf_subtractFrom(int target, int rhs)
{
	std::string result;

	int tmp = allocateTemp();
	result += bf_assign(tmp, rhs);
	result += bf_movePtr(tmp);
	result += "[-";
	result += bf_movePtr(target);
	result += "-";
	result += bf_movePtr(tmp);
	result += "]";
	result += bf_movePtr(target);
	
	return result;
}

std::string Compiler::bf_incr(int target)
{
	return bf_movePtr(target) + "+";
}

std::string Compiler::bf_decr(int target)
{
	return bf_movePtr(target) + "-";
}

std::string Compiler::bf_multiply(int lhs, int rhs, int result)
{
	std::string ops;
	
	int tmp = allocateTemp(); // can I use rhs if this is a temp?

	ops += bf_setToValue(result, 0);
	ops += bf_assign(tmp, rhs);
	ops += "[-";
	ops += bf_addTo(result, lhs);
	ops += bf_movePtr(tmp);
	ops += "]";
	ops += bf_movePtr(result);

	return ops;
}

std::string Compiler::bf_multiplyBy(int target, int factor)
{
	std::string ops;

	int tmp = allocateTemp();
	ops += bf_multiply(target, factor, tmp);
	ops += bf_assign(target, tmp);

	return ops;
}

std::string Compiler::bf_not(int addr, int result)
{
	std::string ops;

	int tmp = allocateTemp();
	
	ops += bf_setToValue(result, 1);
	ops += bf_assign(tmp, addr);
	ops += "[";
	ops += bf_setToValue(result, 0);
	ops += bf_setToValue(tmp, 0);
	ops += "]";
	ops += bf_movePtr(result);

	return ops;
}

std::string Compiler::bf_and(int lhs, int rhs, int result)
{
	int x = allocateTemp();
	int y = allocateTemp();
	
	std::string ops;
	ops += bf_setToValue(result, 0);
	ops += bf_assign(y, rhs);
	ops += bf_assign(x, lhs);
	ops += "[";
	ops +=     bf_movePtr(y);
	ops +=     "[";
	ops +=         bf_setToValue(result, 1);
	ops +=         bf_setToValue(y, 0);
	ops +=     "]";
	ops +=     bf_setToValue(x, 0);
	ops += "]";
	ops += bf_movePtr(result);

	return ops;
}

std::string Compiler::bf_or(int lhs, int rhs, int result)
{
	int x = allocateTemp();
	int y = allocateTemp();

	std::string ops;
	ops += bf_setToValue(result, 0);
	ops += bf_assign(x, lhs);
	ops += "[";
	ops +=     bf_setToValue(result, 1);
	ops +=     bf_setToValue(x, 0);
	ops += "]";
	ops += bf_assign(y, rhs);
	ops += "[";
	ops +=     bf_setToValue(result, 1);
	ops +=     bf_setToValue(y, 0);
	ops += "]";
	ops += bf_movePtr(result);

	return ops;
}

std::string Compiler::bf_equal(int lhs, int rhs, int result)
{
	std::string ops;

	int tmpL = allocateTemp();
	int tmpR = allocateTemp();

	ops += bf_setToValue(result, 1);
	ops += bf_assign(tmpR, rhs);
	ops += bf_assign(tmpL, lhs);
	ops += "[";
	ops +=     bf_decr(tmpR);
	ops +=     bf_decr(tmpL);
	ops += "]";
	ops += bf_movePtr(tmpR);
	ops += "[";
	ops +=     bf_setToValue(result, 0);
	ops +=     bf_setToValue(tmpR, 0);
	ops += "]";
	ops += bf_movePtr(result);

	return ops;
}

std::string Compiler::bf_notEqual(int lhs, int rhs, int result)
{
	std::string ops;
	int result2 = allocateTemp();
	
	ops += bf_equal(lhs, rhs, result2);
	ops += bf_not(result2, result);

	return ops;
}

std::string Compiler::bf_greater(int lhs, int rhs, int result)
{
	int x = allocateTemp();
	int y = allocateTemp();
	int tmp1 = allocateTemp();
	int tmp2 = allocateTemp();
		
	std::string ops;
	ops += bf_setToValue(tmp1, 0);
	ops += bf_setToValue(tmp2, 0);
	ops += bf_setToValue(result, 0);
	ops += bf_assign(y, rhs);
	ops += bf_assign(x, lhs);
	ops += "[";
	ops +=     bf_incr(tmp1);
	ops +=     bf_movePtr(y);
	ops +=     "[";
	ops +=         bf_setToValue(tmp1, 0);
	ops +=         bf_incr(tmp2);
	ops +=         bf_decr(y);
	ops +=     "]";
	ops +=     bf_movePtr(tmp1);
	ops +=     "[";
	ops +=         bf_incr(result);
	ops +=         bf_decr(tmp1);
	ops +=     "]";
	ops +=     bf_movePtr(tmp2);
	ops +=     "[";
	ops +=         bf_incr(y);
	ops +=         bf_decr(tmp2);
	ops +=     "]";
	ops +=     bf_decr(y);
	ops +=     bf_decr(x);
	ops += "]";
	ops += bf_movePtr(result);
	
	return ops;
}

std::string Compiler::bf_less(int lhs, int rhs, int result)
{
	return bf_greater(rhs, lhs, result);
}

std::string Compiler::bf_greaterOrEqual(int lhs, int rhs, int result)
{
	int isEqual = allocateTemp();
	int isGreater = allocateTemp();

	std::string ops;
	ops += bf_equal(lhs, rhs, isEqual);
	ops += bf_greater(lhs, rhs, isGreater);
	ops += bf_or(isEqual, isGreater, result);
	ops += bf_movePtr(result);

	return ops;
}

std::string Compiler::bf_lessOrEqual(int lhs, int rhs, int result)
{
	return bf_greaterOrEqual(rhs, lhs, result);
}


int Compiler::inlineBF(std::string const &code)
{
	assert(validateInlineBF(code) &&
		   "Inline BF must not alter pointer-position. "
		   "Left and right shifts must cancel within each set of []"
		   );
	
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
	int tmp = allocateTemp();
	int addr = addressOf(ident);
	assert(addr >= 0 && "Unknown identifier passed to sizeof()");
	
	int sz = d_memory.sizeOf(addr);
	d_codeBuffer << bf_setToValue(tmp, sz);
	return tmp;
}

int Compiler::movePtr(std::string const &ident)
{
	int addr = addressOf(ident);
	assert(addr >= 0 && "Unknown identifier");

	d_codeBuffer << bf_movePtr(addr);
	return -1;
}

int Compiler::statement(Instruction const &instr)
{
	instr();
	freeTemps();
	return -1;
}

int Compiler::call(std::string const &functionName,
				   std::vector<Instruction> const &args)
{
	auto it = d_functionMap.find(functionName);
	if (it  == d_functionMap.end())
		assert(false && "Error: unknown function.");
	BFXFunction &func = it->second;

	// Check for recursion
	if (std::find(d_callStack.begin(), d_callStack.end(), functionName) != d_callStack.end())
		throw std::string("Error: recursion not allowed.");
			   
	// Allocate parameters and copy arguments into them
	auto const &params = func.params();
	assert(params.size() == args.size() && "Calling function with invalid number of arguments");
	for (size_t idx = 0; idx != args.size(); ++idx)
	{
		// Evaluate argument that's passed in and get its size
		int argAddr = args[idx]();
		assert(argAddr >= 0 && "Unknown variable in argument to function");
		uint8_t sz = d_memory.sizeOf(argAddr);
		
		// Allocate local variable for the function of the correct size
		std::string const &p = params[idx];
		int paramAddr = d_memory.allocateLocalSafe(p, func.name(), sz);

		// Assign argument to parameter
		assign(paramAddr, argAddr);
	}
	
	// Execute body of the function
	d_callStack.push_back(func.name());
	func.body()();
	d_callStack.pop_back();

	// Move return variable to local scope before cleaning up (if non-void)
	std::string retVar = func.returnVariable();
	int ret = -1;
	if (retVar != BFXFunction::VOID)
	{
		ret = d_memory.findLocal(retVar, func.name());
		assert(ret != -1 && "return value not found");

		// Pull the variable into local scope as a temp
		d_memory.changeScope(ret, d_callStack.back());
		d_memory.markAsTemp(ret);
	}

	// Clean up and return
	d_memory.freeLocals(func.name());

	return ret;
}

int Compiler::variable(std::string const &ident, uint8_t sz, bool checkSize)
{
	if (isCompileTimeConstant(ident))
		return constVal(compileTimeConstant(ident));
	
	assert(sz > 0 && "variable size must be at least 1");
	int arr = allocateOrGet(ident, sz);
	if (checkSize)
		assert(d_memory.sizeOf(arr) == sz && "Specified size of array does not match value in memory");
	return arr;
}

int Compiler::constVal(uint8_t num)
{
	int tmp = allocateTemp();
	d_codeBuffer << bf_setToValue(tmp, num);
   	return tmp;
}

int Compiler::assign(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
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
		assert(false && "Cannot assign variables of unequal sizes");
	}

	return lhs;
}

int Compiler::assignPlaceholder(std::string const &lhs, AddressOrInstruction const &rhs)
{
	assert(addressOf(lhs) == -1 && "Placeholder-size can not be attached to already existing variable");
	
	int const sz = d_memory.sizeOf(rhs);
	int lhsAddr = allocateOrGet(lhs, sz);
	return assign(lhsAddr, rhs);
}


int Compiler::arrayFromSizeStaticValue(uint8_t sz, uint8_t val)
{
	int arr = allocateTemp(sz);
	for (int idx = 0; idx != sz; ++idx)
	{
		d_codeBuffer << bf_setToValue(arr + idx, val);
	}

	return arr;
}


int Compiler::arrayFromSizeDynamicValue(uint8_t sz, AddressOrInstruction const &val)
{
	assert(d_memory.sizeOf(val) == 1 &&
		   "Array fill value must refer to a variable of size 1");
	
	return assign(allocateTemp(sz), val);
}

int Compiler::arrayFromList(std::vector<Instruction> const &list)
{
	uint8_t sz = list.size();
	int start = allocateTemp(sz);
	for (int idx = 0; idx != sz; ++idx)
		d_codeBuffer << bf_assign(start + idx, list[idx]());

	return start;
}

int Compiler::arrayFromString(std::string const &str)
{
	uint8_t sz = str.size();
	int start = allocateTemp(sz);
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
	uint8_t const sz = d_memory.sizeOf(arr);

	int const bufSize = sz + 2;
	int const buf = allocateTemp(bufSize);
	int const dist = buf - arr;

	std::string const arr2buf(std::abs(dist), (dist > 0 ? '>' : '<'));
	std::string const buf2arr(std::abs(dist), (dist > 0 ? '<' : '>'));

	// Copy index to buf[0] and buf[1]
	// Zero out remaining cells
	d_codeBuffer << bf_assign(buf + 0, index)
				 << bf_assign(buf + 1, buf)
				 << bf_setToValue(buf + 2, 0, bufSize - 2);

	// NOTE: code below will modify pointer without changing the internal
	// d_pointer value. Must make sure it points to buf[0] after the algorithm.
			   
	// Move i steps within the buffer

	// Pointer is now pointing to index i within the buffer
	// --> move back to the array to point at the element
	// --> move back and forth between array and buffer and move
	//     the value from the array to the temp within the buffer
	// --> Restore the original value in the array by moving the temp
	//     to both the buffer and the array.

	d_codeBuffer << bf_movePtr(buf)
				 << dynamicMoveRight
				 << buf2arr
				 << "[-"
				 << arr2buf
				 << ">>+<<"
				 << buf2arr
				 << "]"
				 << arr2buf
				 << ">>[-<<+"
				 << buf2arr
				 << "+"
				 << arr2buf
				 << ">>]<"
				 << dynamicMoveLeft;

	// Pointer pointing at buf[0] which now contains the element that needed
	// to be fetched.

	// Return first element, containing fetched element
	int const ret = allocateTemp();
	d_codeBuffer << bf_assign(ret, buf);
	return ret;
}

int Compiler::assignElement(std::string const &ident, AddressOrInstruction const &index, AddressOrInstruction const &rhs)
{
	static std::string const dynamicMoveRight = "[>>[->+<]<[->+<]<[->+<]>-]";
	static std::string const dynamicMoveLeft = "[[-<+>]<-]<";
			   
	int const arr = addressOf(ident);
	uint8_t const sz = d_memory.sizeOf(arr);

	int const bufSize = sz + 2;
	int const buf = allocateTemp(bufSize);
	int const dist = buf - arr;

	std::string const arr2buf(std::abs(dist), (dist > 0 ? '>' : '<'));
	std::string const buf2arr(std::abs(dist), (dist > 0 ? '<' : '>'));

	// Copy the index to the first and second element. The value to be assigned to the third.
	// Zero out remaining cells
	d_codeBuffer << bf_assign(buf, index)
				 << bf_assign(buf + 1, buf)
				 << bf_assign(buf + 2, rhs)
				 << bf_setToValue(buf + 3, 0, bufSize - 3);

	// Move the correct amount to the right and set the corresponding cell in the array to 0
	// Move the value from the buffer into the target cell and move back to buf[0]
	d_codeBuffer << bf_movePtr(buf)
				 << dynamicMoveRight
				 << buf2arr
				 << "[-]"
				 << arr2buf
				 << ">>[-<<"
				 << buf2arr
				 << "+"
				 << arr2buf
				 << ">>]<"
				 << dynamicMoveLeft;

	// Pointer pointing at buf[0] --> d_pointer in a valid state. 
	
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
	d_codeBuffer << bf_incr(target);
	return target;
}

int Compiler::preDecrement(AddressOrInstruction const &target)
{
	d_codeBuffer << bf_decr(target);
	return target;
}

int Compiler::postIncrement(AddressOrInstruction const &target)
{
	int const tmp = allocateTemp();
	d_codeBuffer << bf_assign(tmp, target)
				 << bf_incr(target);

	return tmp;
}

int Compiler::postDecrement(AddressOrInstruction const &target)
{
	int const tmp = allocateTemp();
	d_codeBuffer << bf_assign(tmp, target)
				 << bf_decr(target);

	return tmp;
}

int Compiler::addTo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	d_codeBuffer << bf_addTo(lhs, rhs);
	return lhs;
}

int Compiler::add(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const ret = allocateTemp();
	d_codeBuffer << bf_assign(ret, lhs)
				 << bf_addTo(ret, rhs);
	return ret;
}

int Compiler::subtractFrom(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	d_codeBuffer << bf_subtractFrom(lhs, rhs);
	return lhs;
}

int Compiler::subtract(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const ret = allocateTemp();
	d_codeBuffer << bf_assign(ret, lhs)
				 << bf_subtractFrom(ret, rhs);
	return ret;
}

int Compiler::multiplyBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	d_codeBuffer << bf_multiplyBy(lhs, rhs);
	return lhs;
}

int Compiler::multiply(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const ret = allocateTemp();
	d_codeBuffer << bf_multiply(lhs, rhs, ret);
	return ret;
}

int Compiler::divideBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	return assign(lhs, divide(lhs, rhs));
}

int Compiler::divide(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	return divModPair(lhs, rhs).first;
}

int Compiler::moduloBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	return assign(lhs, modulo(lhs, rhs));
}

int Compiler::modulo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	return divModPair(lhs, rhs).second;
}

int Compiler::divMod(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	auto const pr = divModPair(lhs, rhs);
	assign(lhs, pr.first);
	return pr.second;
}

int Compiler::modDiv(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	auto const pr = divModPair(lhs, rhs);
	assign(lhs, pr.second);
	return pr.first;
}

std::pair<int, int> Compiler::divModPair(AddressOrInstruction const &num, AddressOrInstruction const &denom)
{
	int const tmp = allocateTemp(4);
    int const tmp_loopflag  = tmp + 0;
    int const tmp_zeroflag  = tmp + 1;
    int const tmp_num       = tmp + 2;
    int const tmp_denom     = tmp + 3;
	
    int const result_div    = allocateTemp();
    int const result_mod    = allocateTemp();
	
    // Algorithm:
	// 1. Initialize result-cells to 0 and copy operands to temps
	// 2. In case the denominator is 0 (divide by zero), set the result to 255 (~inf)
	//    Set the loopflag to 0 in order to skip the calculating loop.
    // 3. In case the numerator is 0, the result of division is also zero. Set the remainder
	//    to the same value as the denominator.
	// 4. Enter the loop:
	//	  *  On each iteration, decrement both the denominator and enumerator,
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
	int const result = allocateTemp();
	d_codeBuffer << bf_equal(lhs, rhs, result);
	return result;
}

int Compiler::notEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_notEqual(lhs, rhs, result);
	return result;
}

int Compiler::less(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_less(lhs, rhs, result);
	return result;
}

int Compiler::greater(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_greater(lhs, rhs, result);
	return result;
}

int Compiler::lessOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_lessOrEqual(lhs, rhs, result);
	return result;
}

int Compiler::greaterOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_greaterOrEqual(lhs, rhs, result);
	return result;
}

int Compiler::logicalNot(AddressOrInstruction const &arg)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_not(arg, result);

	return result;
}

int Compiler::logicalAnd(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_and(lhs, rhs, result);
	return result;
}

int Compiler::logicalOr(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs)
{
	int const result = allocateTemp();
	d_codeBuffer << bf_or(lhs, rhs, result);
	return result;
}

int Compiler::ifStatement(Instruction const &condition, Instruction const &ifBody, Instruction const &elseBody)
{
	int const cond = condition(); 
	int const elseCond = logicalNot(cond);

	pushStack(cond);
	pushStack(elseCond); 

	d_codeBuffer << bf_movePtr(cond)
				 << "[";

	ifBody();
	
	d_codeBuffer << bf_setToValue(cond, 0)
				 << "]"
				 <<  bf_movePtr(elseCond)
				 << "[";

	elseBody();
	
	d_codeBuffer << bf_setToValue(elseCond, 0)
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
	init();
	
	int const cond1 = condition();
	pushStack(cond1);
	d_codeBuffer << bf_movePtr(cond1)
				 << "[";

	body();
	increment();
	int const cond2 = condition();
	
	d_codeBuffer << bf_assign(cond1, cond2)
				 << "]";

	popStack();
	
	return -1;
}

int Compiler::whileStatement(Instruction const &condition, Instruction const &body)
{
	int const cond1 = condition();
	pushStack(cond1);
	d_codeBuffer << bf_movePtr(cond1)
				 << "[";
	body();

	int const cond2 = condition();
	d_codeBuffer << bf_assign(cond1, cond2)
				 << "]";

	popStack();
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

	std::string result	= cancel(bf, '>', '<');
	return cancel(result, '+', '-');
}
