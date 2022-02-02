#include "memory.h"

void Memory::Cell::assign(std::string const &ident,
                          std::string const &scope,
                          int const sz,
                          CellSpec const type)
{
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
        
int Memory::findFree(int const sz)
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

int Memory::getTemp(std::string const &scope, int const sz)
{
    int start = findFree(sz);
    d_memory[start].assign("", scope, sz, CellSpec::TEMP);
    for (int i = 1; i != sz; ++i)
        d_memory[start + i].assign("", "", 0, CellSpec::REFERENCED);

    return start;
}

int Memory::getTempBlock(std::string const &scope, int const sz)
{
    int start = findFree(sz);
    for (int i = 0; i != sz; ++i)
        d_memory[start + i].assign("", scope, 1, CellSpec::TEMP);

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

int Memory::allocateLocalSafe(std::string const &ident, std::string const &scope, int const sz)
{
    assert(findLocal(scope, ident) == -1 && "Variable already allocated locally");
    return allocateLocalUnsafe(ident, scope, sz);
}

int Memory::allocateGlobalSafe(std::string const &ident, int const sz)
{
    assert(findGlobal(ident) == -1 && "Variable already allocated globally");
    return allocateGlobalUnsafe(ident, sz);
}


int Memory::allocateLocalUnsafe(std::string const &ident, std::string const &scope, int const sz)
{
    int addr = findFree(sz);
    d_memory[addr].assign(ident, scope, sz, CellSpec::LOCAL);
    for (int i = 1; i != sz; ++i)
        d_memory[addr + i].assign("", "", 0, CellSpec::REFERENCED);

    return addr;
}

int Memory::allocateGlobalUnsafe(std::string const &ident, int const sz)
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


void Memory::stack(int const addr)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    // assert((d_memory[addr].isLocal() || d_memory[addr].isTemp()) &&
    //        "Only local variables can be stacked.");

    d_memory[addr].stack();
}

void Memory::unstack(int const addr)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    assert(d_memory[addr].isStacked() && "Only stacked variables can be unstacked.");

    d_memory[addr].unstack();
}

std::string Memory::cellString(int const addr) const
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
    freeIf([&](Cell const &cell){
               return cell.isTemp() && cell.scope() == scope;
           });
}

void Memory::freeLocals(std::string const &scope)
{
    freeIf([&](Cell const &cell){
               return cell.scope() == scope;
           });
}

int Memory::sizeOf(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell const &cell = d_memory[addr];
    assert(!cell.isEmpty() && "Requested size of empty address");
    
    return d_memory[addr].size();
}

void Memory::markAsTemp(int const addr)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell &cell = d_memory[addr];
    cell.assign("", cell.scope(), cell.size(), CellSpec::TEMP);
}


void Memory::markAsGlobal(int const addr)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell &cell = d_memory[addr];
    cell.assign(cell.ident(),"", cell.size(), CellSpec::GLOBAL);
}

void Memory::changeScope(int const addr, std::string const &newScope)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell &cell = d_memory[addr];
    cell.assign(cell.ident(), newScope, cell.size(), cell.type());
}

void Memory::changeIdent(int const addr, std::string const &newIdent)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell &cell = d_memory[addr];
    cell.assign(newIdent, cell.scope(), cell.size(), cell.type());
}

std::string Memory::identifier(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].ident();
}

bool Memory::isTemp(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].isTemp();
}

