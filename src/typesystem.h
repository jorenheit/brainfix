#ifndef TYPESYSTEM_H
#define TYPESYSTEM_H

#include <map>
#include <variant>
#include <vector>
#include <cassert>

namespace TypeSystem
{
    struct Field;
    
    class Type
    {
        enum class Kind
            {
             NULLTYPE,
             INT,
             STRUCT
            };

        int d_size{-1};
        std::string d_name{""};
        Kind d_kind{Kind::NULLTYPE};
        
    public:
        Type() = default;
        
        Type(std::string const &name):
            d_name(name),
            d_kind(Kind::STRUCT)
        {}

        Type(int const sz):
            d_size(sz),
            d_kind(Kind::INT)
        {}

        int size() const;
        std::string name() const;
        bool defined() const;
        std::vector<Field> fields() const;
        bool operator==(Type const &other) const;
        bool isIntType() const;
        bool isStructType() const;
        bool isNullType() const;
    };

    struct Field
    {
        std::string name;
        int         offset;
        Type        type;
    };


    bool add(std::string const &name,
             std::vector<std::pair<std::string, Type>> const &fields);
};

#endif // TYPES_H


