#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include <iostream>

class Memory
{
public:	
	enum class CellSpec
		{
		 EMPTY,
		 LOCAL,
		 TEMP,
		 REFERENCED,
		 STACKED,
		 GLOBAL,
		 VOID,
		 INVALID
		};

private:
	class Cell
	{
		std::string d_ident;
		std::string d_scope;
		uint8_t d_sz;
		CellSpec d_type;
		CellSpec d_restore;
		
	public:
		Cell():
			d_type(CellSpec::EMPTY),
			d_restore(CellSpec::INVALID)
		{}
		
		void clear();
		void assign(std::string const &ident, std::string const &scope,
					uint8_t sz, CellSpec type);
		void stack();
		void unstack();
			
		std::string const &ident() const { return d_ident; }
		std::string const &scope() const { return d_scope; }
		uint8_t size() const { return d_sz; }
		CellSpec type() const { return d_type; }
		
		bool isEmpty() const { return d_type == CellSpec::EMPTY; }
		bool isLocal() const { return d_type == CellSpec::LOCAL; }
		bool isGlobal() const { return d_type == CellSpec::GLOBAL; }
		bool isStacked() const { return d_type == CellSpec::STACKED; }
		bool isTemp() const { return d_type == CellSpec::TEMP; }
		bool isReferenced() const { return d_type == CellSpec::REFERENCED; }
		
	};

	std::vector<Memory::Cell> d_memory;

public:
	Memory(size_t sz):
		d_memory(sz)
	{}

	int getTemp(std::string const &scope, uint8_t sz = 1);
	int allocateLocalSafe(std::string const &ident, std::string const &scope, uint8_t sz = 1);
	int allocateGlobalSafe(std::string const &ident, uint8_t sz = 1);
	int allocateLocalUnsafe(std::string const &ident, std::string const &scope, uint8_t sz = 1);
	int allocateGlobalUnsafe(std::string const &ident, uint8_t sz = 1);
	int findLocal(std::string const &ident, std::string const &scope);
	int findGlobal(std::string const &ident);
	uint8_t sizeOf(int addr) const;
	void stack(int addr);
	void unstack(int addr);
	void freeTemps(std::string const &scope);
	void freeLocals(std::string const &scope);
	void markAsTemp(int addr);
	void markAsGlobal(int addr);
	
	
	size_t size() const { return d_memory.size(); }
	std::string cellString(int addr) const;



private:	
	int findFree(uint8_t sz = 1);
};

#endif
