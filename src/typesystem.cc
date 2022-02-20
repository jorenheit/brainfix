#include <iostream>
#include "typesystem.h"

std::map<std::string, TypeSystem::StructDefinition> TypeSystem::typeMap;

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
    if (isStructType())
        return d_name;

    return TypeSystem::intName(d_size);
}

bool TypeSystem::Type::defined() const
{
    if (isIntType())
        return true;
            
    auto const it = TypeSystem::typeMap.find(name());
    return it != TypeSystem::typeMap.end();
}
        
TypeSystem::StructDefinition TypeSystem::Type::definition() const
{
    assert(defined() &&
           "requesting definition of undefined type. Call defined() before calling definition()");

    if (isStructType())
        return typeMap.at(name());

    return StructDefinition(size());
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

 
void TypeSystem::StructDefinition::addField(NameTypePair const &f)
{
    d_fields.emplace_back(Field{f.first, d_size, f.second});
    d_size += f.second.size();
}

TypeSystem::StructDefinition::StructDefinition(int const sz):
    d_size(0),
    d_name(TypeSystem::intName(sz))
{
    addField(NameTypePair{"", Type(sz)});
}

TypeSystem::StructDefinition::StructDefinition(std::string const &name):
    d_size(1),
    d_name(name)
{}


int TypeSystem::StructDefinition::size() const
{
    return d_size;
}

std::vector<TypeSystem::StructDefinition::Field> const &TypeSystem::StructDefinition::fields() const
{
    return d_fields;
}

std::string const &TypeSystem::StructDefinition::name() const
{
    return d_name;
}

bool TypeSystem::add(std::string const &name, std::vector<NameTypePair> const &fields)
{
    if (typeMap.find(name) != typeMap.end())
        return false;
        
    StructDefinition s(name);
    for (auto const &f: fields)
    {
        s.addField(f);
    }

    typeMap.insert({name, s});
    return true;
}
