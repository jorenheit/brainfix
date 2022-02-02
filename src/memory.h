#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include <iostream>
#include <functional>
#include <cassert>

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
        int d_sz;
        CellSpec d_type;
        CellSpec d_restore;
        
    public:
        Cell():
            d_type(CellSpec::EMPTY),
            d_restore(CellSpec::INVALID)
        {}
        
        void clear();
        void assign(std::string const &ident, std::string const &scope,
                    int const sz, CellSpec const type);
        void stack();
        void unstack();
            
        std::string const &ident() const { return d_ident; }
        std::string const &scope() const { return d_scope; }
        int size() const { return d_sz; }
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

    size_t size() const;
    int getTemp(std::string const &scope, int const sz = 1);
    int getTempBlock(std::string const &scope, int const sz);
    int allocateLocalSafe(std::string const &ident, std::string const &scope, int const sz = 1);
    int allocateGlobalSafe(std::string const &ident, int const sz = 1);
    int allocateLocalUnsafe(std::string const &ident, std::string const &scope, int const sz = 1);
    int allocateGlobalUnsafe(std::string const &ident, int const sz = 1);
    int findLocal(std::string const &ident, std::string const &scope);
    int findGlobal(std::string const &ident);
    int sizeOf(int const addr) const;
    void stack(int const addr);
    void unstack(int const addr);
    void freeTemps(std::string const &scope);
    void freeLocals(std::string const &scope);
    void markAsTemp(int const addr);
    void markAsGlobal(int const addr);
    void changeScope(int const addr, std::string const &newScope);
    void changeIdent(int const addr, std::string const &newIdent);
    std::string identifier(int const addr) const;
    std::string cellString(int const addr) const;
    bool isTemp(int const addr) const;

private:    
    int findFree(int sz = 1);

    template <typename Predicate>
    void freeIf(Predicate &&pred);
};

inline size_t Memory::size() const
{
    return d_memory.size();
}

template <typename Predicate>
void Memory::freeIf(Predicate&& pred)
{
    for (size_t idx = 0; idx != d_memory.size(); ++idx)
    {
        Cell &cell = d_memory[idx];
        if (pred(cell))
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


#endif
