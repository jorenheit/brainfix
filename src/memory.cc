#include "memory.h"

void Memory::Cell::backup()
{
    d_backupStack.push(Members{
                               identifier,
                               scope,
                               content
        });

    // cells that have been backed up are protected -> cannot be cleared
    content = Content::PROTECTED;
}

void Memory::Cell::restore()
{
    assert(d_backupStack.size() > 0 && "restore called on non-backed-up cell");
    
    Members const &m = d_backupStack.top();
    identifier = std::get<0>(m);
    scope      = std::get<1>(m);
    content    = std::get<2>(m);

    d_backupStack.pop();
}
            
void Memory::Cell::clear()
{
    assert(content != Content::PROTECTED && "tried to clear a protected cell");
    
    identifier.clear();
    scope.clear();
    content = Content::EMPTY;
    type = TypeSystem::Type{};
    value = 0;
    synced = false;
}
        
int Memory::findFree(int const sz)
{
    for (size_t start = 0; start != d_memory.size() - sz; ++start)
    {
        bool done = true;
        for (int offset = 0; offset != sz; ++offset)
        {
            if (!d_memory[start + offset].empty())
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

int Memory::getTemp(std::string const &scope, TypeSystem::Type type)
{
    return allocate("", scope, type);
}

int Memory::getTemp(std::string const &scope, int const sz)
{
    return getTemp(scope, TypeSystem::Type(sz));
}

int Memory::getTempBlock(std::string const &scope, int const sz)
{
    int start = findFree(sz);
    for (int i = 0; i != sz; ++i)
    {
        Cell &cell = d_memory[start + i];
        cell.clear();
        cell.scope = scope;
        cell.type = TypeSystem::Type(1);
        cell.content = Content::TEMP;
    }

    return start;
}

int Memory::allocate(std::string const &ident, std::string const &scope, TypeSystem::Type type)
{
    assert(type.defined() && "Trying to allocate undefined type");
    
    if (!ident.empty() && find(ident, scope) != -1)
        return  -1;

    int const addr = findFree(type.size());
    if (addr + type.size() > d_maxAddr)
        d_maxAddr = addr + type.size();
    
    Cell &cell = d_memory[addr];
    cell.clear();
    cell.identifier = ident;
    cell.scope = scope;
    cell.content = ident.empty() ? Content::TEMP : Content::NAMED;
    cell.type = type;
    
    place(type, addr);
    return addr;
}

void Memory::place(TypeSystem::Type type, int const addr, bool const recursive)
{
    if (type.isIntType())
    {
        for (int i = 1; i != type.size(); ++i)
        {
            Cell &cell = d_memory[addr + i];
            cell.clear();
            cell.type = TypeSystem::Type(1);
            cell.content = Content::REFERENCED;
        }
        return;
    }

    if (recursive)
    {
        Cell &cell = d_memory[addr];
        cell.clear();
        cell.content = Content::REFERENCED;
        cell.type = type;
    }
        
    auto const &definition = type.definition();
    for (auto const &f: definition.fields())
    {
        if (f.type.isStructType())
        {
            place(f.type, addr + f.offset, true);
            continue;
        }

        Cell &cell = d_memory[addr + f.offset];
        cell.clear();
        cell.type = f.type;
        cell.content = Content::REFERENCED;

        for (int i = 1; i != f.type.size(); ++i)
        {
            Cell &cell = d_memory[addr + f.offset + i];
            cell.clear();
            cell.type = TypeSystem::Type(1);
            cell.content = Content::REFERENCED;
        }
    }
}

int Memory::find(std::string const &ident, std::string const &scope) const
{
    auto const it = std::find_if(d_memory.begin(), d_memory.end(),
                                 [&](Cell const &cell)
                                 {
                                     return scope.find(cell.scope) == 0 &&
                                         cell.identifier == ident;
                                 });
    
    if (it != d_memory.end())
        return it - d_memory.begin();

    return -1;
}

void Memory::push(int const addr)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    d_memory[addr].backup();
    d_protectedStack.push(addr);
}

int Memory::pop()
{
    int const addr = d_protectedStack.top();
    d_memory[addr].restore();
    d_protectedStack.pop();
    return addr;
}

void Memory::freeTemps(std::string const &scope)
{
    freeIf([&](Cell const &cell){
               return cell.content == Content::TEMP &&
                   cell.scope == scope;
           });
}

void Memory::freeLocals(std::string const &scope)
{
    freeIf([&](Cell const &cell){
               return cell.scope == scope;
           });
}

int Memory::sizeOf(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell const &cell = d_memory[addr];
    assert(!cell.empty() && "Requested size of empty address");
    return cell.size();
}

int Memory::sizeOf(std::string const &ident, std::string const &scope) const
{
    int const addr = find(ident, scope);
    return (addr >= 0) ? d_memory[addr].size() : 0;
}

void Memory::markAsTemp(int const addr)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell &cell = d_memory[addr];
    
    assert(cell.content != Content::PROTECTED && "calling markAsTemp on protected cell");
    cell.identifier = "";
    cell.content = Content::TEMP;
}

void Memory::rename(int const addr, std::string const &ident, std::string const &scope)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    Cell &cell = d_memory[addr];
    cell.identifier = ident;
    cell.scope = scope;

    if (cell.content != Content::PROTECTED)
        cell.content = Content::NAMED;
}

std::string Memory::identifier(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].identifier;
}

std::string Memory::scope(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].scope;
}

bool Memory::isTemp(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].content == Content::TEMP;
}

TypeSystem::Type Memory::type(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].type;
}

TypeSystem::Type Memory::type(std::string const &ident, std::string const &scope) const
{
    int const addr = find(ident, scope);
    return d_memory[addr].type;
}

int Memory::value(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].value;
}

int &Memory::value(int const addr) 
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].value;
}

bool Memory::valueUnknown(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].value == -1;
}

void Memory::setValueUnknown(int const addr)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    d_memory[addr].value = -1;
    d_memory[addr].synced = false;
}

void Memory::setSync(int const addr, bool val)
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    d_memory[addr].synced = val;
}

bool Memory::isSync(int const addr) const
{
    assert(addr >= 0 && addr < (int)d_memory.size() && "address out of bounds");
    return d_memory[addr].synced;
}

std::vector<int> Memory::cellsInScope(std::string const &scope) const
{
    std::vector<int> result;
    for (int i = 0; i != d_maxAddr; ++i)
    {
        if (scope.find(d_memory[i].scope) == 0)
            result.push_back(i);
    }
    
    return result;
}


void Memory::dump() const
{
    static std::string const contentStrings[] =
        {
         "EMPTY",
         "NAMED",
         "TEMP",
         "REFERENCED",
         "PROTECTED"
        };
    
    std::cerr << "addr  |  var  |  scope  |  type  | content  |  value  |  synced | \n";
    for (int i = 0; i != d_maxAddr; ++i)
    {
        Cell const &c = d_memory[i];
        if (c.content == Content::EMPTY)
            continue;
        
        std::cerr << i << "\t" << c.identifier <<  '\t' << c.scope << '\t'
                  << c.type.name() << '\t' << contentStrings[static_cast<int>(c.content)] << '\t'
                  << c.value << '\t' << (c.synced ? "SYNCED" : "DESYNCED") << '\n';
    }
}
