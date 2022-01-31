#ifndef COMPILER_H
#define COMPILER_H

#include <fstream>
#include <cassert>
#include <string>
#include <map>
#include <deque>
#include <stack>
#include <sstream>
#include <variant>
#include "memory.h"
#include "parser.h"

class Compiler
{
	friend class Parser;
	
	static constexpr size_t TAPE_SIZE_INITIAL = 1000;

	Parser d_parser;
	size_t d_pointer{0};
	Memory d_memory;

	std::map<std::string, BFXFunction> d_functionMap;
	std::map<std::string, uint8_t> d_constMap;
	std::deque<std::string> d_callStack;
	std::ostringstream d_codeBuffer;
	std::stack<int> d_stack;

public:
	Compiler(std::string const &file):
		d_parser(file, *this),
		d_memory(TAPE_SIZE_INITIAL)
	{}

	int compile();
	void write(std::ostream &out = std::cout);

private:
	void addFunction(BFXFunction bfxFunc, Instruction const &body);
	void addGlobals(std::vector<Instruction> const &vars);
	void addConstant(std::string const &ident, uint8_t num);
	uint8_t compileTimeConstant(std::string const &ident) const;
	bool isCompileTimeConstant(std::string const &ident) const;

	static bool validateInlineBF(std::string const &ident);
	static std::string cancelOppositeCommands(std::string const &bf);
	
	// Memory management uitilities
	int allocateOrGet(std::string const &ident, uint8_t sz = 1);
	int allocateTemp(uint8_t sz = 1);
	int addressOf(std::string const &ident);
	void freeTemps();
	void freeLocals();
	void pushStack(int addr);
	int popStack();

	// BF generators
	std::string bf_movePtr(size_t addr);
	std::string bf_setToValue(int addr, int val);
	std::string bf_setToValue(int start, int val, size_t n);
	std::string bf_assign(int lhs, int rhs);
	std::string bf_assign(int dest, int src, size_t n);
	std::string bf_addTo(int target, int rhs);
	std::string bf_incr(int target);
	std::string bf_decr(int target);
	std::string bf_subtractFrom(int target, int rhs);
	std::string bf_multiply(int lhs, int rhs, int result);
	std::string bf_multiplyBy(int target, int rhs);
	std::string bf_equal(int lhs, int rhs, int result);
	std::string bf_notEqual(int lhs, int rhs, int result);
	std::string bf_greater(int lhs, int rhs, int result);
	std::string bf_less(int lhs, int rhs, int result);
	std::string bf_greaterOrEqual(int lhs, int rhs, int result);
	std::string bf_lessOrEqual(int lhs, int rhs, int result);
	std::string bf_not(int operand, int result);
	std::string bf_and(int lhs, int rhs, int result);
	std::string bf_or(int lhs, int rhs, int result);
	
	// Instruction generator
	template <auto Member, typename ... Args>
	Instruction instruction(Args ... args){
		return [=, this](){ return (this->*Member)(args ...); };
	}

	// Wrappers for element-modifying instructions
	using UnaryFunction = int (Compiler::*)(AddressOrInstruction const &);
	int applyUnaryFunctionToElement(std::string const &arrayIdent,
									AddressOrInstruction const &index,
									UnaryFunction func);
	
	using BinaryFunction = int (Compiler::*)(AddressOrInstruction const &, AddressOrInstruction const &);
	int applyBinaryFunctionToElement(std::string const &arrayIdent,
									 AddressOrInstruction const &index,
									 AddressOrInstruction const &rhs,
									 BinaryFunction func);

	// Instructions
	int inlineBF(std::string const &str);
	int sizeOfOperator(std::string const &ident);
	int movePtr(std::string const &ident);
	int variable(std::string const &ident, uint8_t sz, bool checkSize);
	int constVal(uint8_t val);
	int statement(Instruction const &instr);
	int mergeInstructions(Instruction const &instr1, Instruction const &instr2);
	int arrayFromSizeStaticValue(uint8_t sz, uint8_t val = 0);
	int arrayFromSizeDynamicValue(uint8_t sz, AddressOrInstruction const &val);
	int arrayFromList(std::vector<Instruction> const &list);
	int arrayFromString(std::string const &str);
	int call(std::string const &functionName, std::vector<Instruction> const &args = {});	
	int assign(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int assignPlaceholder(std::string const &lhs, AddressOrInstruction const &rhs);
	int assignElement(std::string const &ident, AddressOrInstruction const &index, AddressOrInstruction const &rhs);
	int fetchElement(std::string const &ident, AddressOrInstruction const &index);
	int preIncrement(AddressOrInstruction const &addr);
	int preDecrement(AddressOrInstruction const &addr);
	int postIncrement(AddressOrInstruction const &addr);
	int postDecrement(AddressOrInstruction const &addr);
	int addTo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int add(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int subtractFrom(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int subtract(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int multiplyBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int multiply(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int equal(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int notEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int less(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int greater(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int lessOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int greaterOrEqual(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int logicalNot(AddressOrInstruction const &arg);
	int logicalAnd(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int logicalOr(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int divideBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int divide(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int moduloBy(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int modulo(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int divMod(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	int modDiv(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);
	std::pair<int, int> divModPair(AddressOrInstruction const &lhs, AddressOrInstruction const &rhs);

	int ifStatement(Instruction const &condition, Instruction const &ifBody, Instruction const &elseBody);	
	int forStatement(Instruction const &init, Instruction const &condition,
					 Instruction const &increment, Instruction const &body);	
	int whileStatement(Instruction const &condition, Instruction const &body);


	template <typename ... Args>
	void errorIf(bool condition, Args&& ... args) const
	{
		if (!condition) return;
		
		std::cerr << "Error in " << d_parser.filename() << " on line " << d_parser.lineNr() << ": ";
		if constexpr (sizeof ... (args) > 0)
			(std::cerr << ... << args) << '\n';

		try {
			d_parser.ERROR();
		}
		catch (...)
		{
			std::cerr << "Unable to recover: compilation terminated\n";
			std::exit(1);
		}
	}

	template <typename ... Args>
	void validateAddr__(std::string const &function, Args&& ... args) const
	{
		if (((args < 0) || ...))
		{
			std::cerr << "Fatal internal error while parsing " << d_parser.filename()
					  << ", line " << d_parser.lineNr()
					  << ": negative address passed to " << function << "()\n\n"
					  << "Compilation terminated\n";
			std::exit(1);
		}
		
		if ((((int)args >= (int)d_memory.size()) || ...))
		{
			std::cerr << "Fatal internal error while parsing " << d_parser.filename()
					  << ", line " << d_parser.lineNr()
					  << ": address out of bounds passed to " << function << "()\n\n"
					  << "Compilation terminated\n";
			
			std::exit(1);
		}
	}
};

#endif //COMPILER_H
