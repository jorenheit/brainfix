#include <cassert>
#include "memory.h"

void Memory::Cell::assign(std::string const &ident,
						  std::string const &scope,
						  uint8_t sz,
						  CellSpec type)
{
	assert(type != CellSpec::EMPTY &&
		   "Assignment of CellSpec::EMPTY not allowed: use clear()");
			
	d_ident = ident;
	d_scope = scope;
	d_sz = sz;
	d_type = type;
}

void Memory::Cell::stack()
{
	d_restore = d_type;
	d_type = CellSpec::STACKED;
}

void Memory::Cell::unstack()
{
	d_type = d_restore;
	d_restore = CellSpec::INVALID;
}

			
void Memory::Cell::clear()
{
	d_ident.clear();
	d_scope.clear();
	d_sz = 0;
	d_type = CellSpec::EMPTY;
	d_restore = CellSpec::INVALID;
}
		
int Memory::findFree(uint8_t sz)
{
	for (size_t start = 0; start != d_memory.size() - sz; ++start)
	{
		bool done = true;
		for (int offset = 0; offset != sz; ++offset)
		{
			if (!d_memory[start + offset].isEmpty())
			{
				done = false;
				break;
			}
		}
        
		if (done) return start;
	}
	
	d_memory.resize(d_memory.size() + sz);
	return findFree(sz);
}

int Memory::getTemp(std::string const &scope, uint8_t sz)
{
	int start = findFree(sz);
	d_memory[start].assign("", scope, sz, CellSpec::TEMP);
	for (int i = 1; i != sz; ++i)
		d_memory[start + i].assign("", "", 0, CellSpec::REFERENCED);

	return start;
}

int Memory::findLocal(std::string const &ident, std::string const &scope)
{
	auto it = std::find_if(d_memory.begin(), d_memory.end(), [&](Cell const &cell)
															 {
																 return (cell.scope() == scope) &&
																	 (cell.ident() == ident);
															 });
	if (it != d_memory.end())
		return it - d_memory.begin();

	return -1;
}

int Memory::allocateLocalSafe(std::string const &ident, std::string const &scope, uint8_t sz)
{
	assert(findLocal(scope, ident) == -1 && "Variable already allocated locally");
	return allocateLocalUnsafe(ident, scope, sz);
}

int Memory::allocateGlobalSafe(std::string const &ident, uint8_t sz)
{
	assert(findGlobal(ident) == -1 && "Variable already allocated globally");
	return allocateGlobalUnsafe(ident, sz);
}


int Memory::allocateLocalUnsafe(std::string const &ident, std::string const &scope, uint8_t sz)
{
	int addr = findFree(sz);
	d_memory[addr].assign(ident, scope, sz, CellSpec::LOCAL);
	for (int i = 1; i != sz; ++i)
		d_memory[addr + i].assign("", "", 0, CellSpec::REFERENCED);

	return addr;
}

int Memory::allocateGlobalUnsafe(std::string const &ident, uint8_t sz)
{
	int addr = findFree(sz);
	d_memory[addr].assign(ident, "", 1, CellSpec::GLOBAL);
	for (int i = 1; i != sz; ++i)
		d_memory[addr + i].assign("", "", 0, CellSpec::REFERENCED);
	
	return addr;
}


int Memory::findGlobal(std::string const &ident)
{
	auto it = std::find_if(d_memory.begin(), d_memory.end(), [&](Cell const &cell)
															 {
																 return cell.type() == CellSpec::GLOBAL &&
																	 cell.ident() == ident;
															 });
	if (it != d_memory.end())
		return it - d_memory.begin();

	return -1;
}


void Memory::stack(int addr)
{
	assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
	assert((d_memory[addr].isLocal() || d_memory[addr].isTemp()) &&
		   "Only local variables can be stacked.");

	d_memory[addr].stack();
}

void Memory::unstack(int addr)
{
	assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
	assert(d_memory[addr].isStacked() && "Only local variables can be protected.");

	d_memory[addr].unstack();
}

std::string Memory::cellString(int addr) const
{
	assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");

	Cell const &cell = d_memory[addr];
	std::string str;
	switch (cell.type())
	{
	case Memory::CellSpec::EMPTY:
		str = "--"; break;
	case Memory::CellSpec::LOCAL:
		str = cell.scope() + "::" + cell.ident(); break;
	case Memory::CellSpec::TEMP:
		str = cell.scope() + "::" + "temp"; break;
	case Memory::CellSpec::REFERENCED:
		str = cell.scope() + "::" + "referenced"; break;
	case Memory::CellSpec::STACKED:
		str = cell.scope() + "::" + "stacked"; break;
	case Memory::CellSpec::GLOBAL:
		str = "GLOBAL::" + cell.ident(); break;
	case Memory::CellSpec::VOID:
		str = "VOID"; break;
	default:
		assert(false && "Unhandled case");
	}

	if (cell.size() > 1)
		str += "*";

	return str;
}

void Memory::freeTemps(std::string const &scope)
{
	for (size_t idx = 0; idx != d_memory.size(); ++idx)
	{
		Cell &cell = d_memory[idx];
		if (cell.isTemp() && cell.scope() == scope)
		{
			for (int offset = 1; offset < cell.size(); ++offset)
			{
				Cell &referenced = d_memory[idx + offset];
				assert(referenced.isReferenced() &&
					   "Trying to free a referenced cell that is not marked as referenced");
				
				referenced.clear();
			}
			cell.clear();
		}
	}
}

void Memory::freeLocals(std::string const &scope)
{
	for (size_t idx = 0; idx != d_memory.size(); ++idx)
	{
		Cell &cell = d_memory[idx];
		if (cell.scope() == scope)
		{
			for (int offset = 1; offset != cell.size(); ++offset)
			{
				Cell &referenced = d_memory[idx + offset];
				assert(referenced.isReferenced() &&
					   "Trying to free a referenced cell that is not marked as referenced");
				
				referenced.clear();
			}
			cell.clear();
		}
	}
}

uint8_t Memory::sizeOf(int addr) const
{
	assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
	Cell const &cell = d_memory[addr];
	assert(!cell.isEmpty() && "Requested size of empty address");
	
	return d_memory[addr].size();
}

void Memory::markAsTemp(int addr)
{
	assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
	Cell &cell = d_memory[addr];
	cell.assign("", cell.scope(), cell.size(), CellSpec::TEMP);
}


void Memory::markAsGlobal(int addr)
{
	assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
	Cell &cell = d_memory[addr];
	cell.assign(cell.ident(),"", cell.size(), CellSpec::GLOBAL);
}

void Memory::changeScope(int addr, std::string const &newScope)
{
	assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
	Cell &cell = d_memory[addr];
	cell.assign(cell.ident(), newScope, cell.size(), cell.type());
}
