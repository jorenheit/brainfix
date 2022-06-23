#include <iostream>
#include "typesystem.h"

namespace TypeSystem
{
    class StructDefinition
    {
    private:
        int d_size;
        bool const d_valid{true};
        std::string d_name;
        std::vector<Field> d_fields;

    public:
        StructDefinition(std::string const &name):
            d_size(1),
            d_name(name)
        {}
        
        StructDefinition():
            d_valid{false}
        {}

        void addField(std::string const &name, Type const &type)
        {
            d_fields.emplace_back(Field{name, d_size, type});
            d_size += type.size();
        }
        
        int size() const
        {
            return d_size;
        }
        
        std::vector<Field> const &fields() const
        {
            return d_fields;
        }
        
        std::string const &name() const
        {
            return d_name;
        }
    };

    static std::map<std::string, StructDefinition> typeMap;

    static std::string intName(int const sz)
    {
        return "__int_" + std::to_string(sz) + "__";
    }
}

int TypeSystem::Type::size() const
{
    if (isIntType())
        return d_size;

    auto const it = TypeSystem::typeMap.find(d_name);
    if (it == TypeSystem::typeMap.end())
        return -1;

    return (it->second).size();
}

std::string TypeSystem::Type::name() const
{
    return isStructType() ? d_name : TypeSystem::intName(d_size);
}

bool TypeSystem::Type::defined() const
{
    if (isIntType() || isNullType())
        return true;
            
    auto const it = TypeSystem::typeMap.find(name());
    return it != TypeSystem::typeMap.end();
}
        
std::vector<TypeSystem::Field> TypeSystem::Type::fields() const
{
    assert(defined() && isStructType() && "requesting fields of undefined type.");

    return typeMap.at(name()).fields();
}

bool TypeSystem::Type::operator==(Type const &other) const
{
    return name() == other.name();
}

bool TypeSystem::Type::isIntType() const
{
    return d_kind == Kind::INT;
}

bool TypeSystem::Type::isStructType() const
{
    return d_kind == Kind::STRUCT;
}

bool TypeSystem::Type::isNullType() const
{
    return d_kind == Kind::NULLTYPE;
}

bool TypeSystem::add(std::string const &name,
                     std::vector<std::pair<std::string, Type>> const &fields)
{
    if (typeMap.find(name) != typeMap.end())
        return false;
        
    StructDefinition s(name);
    for (auto const &f: fields)
    {
        s.addField(f.first, f.second);
    }

    typeMap.insert({name, s});
    return true;
}
