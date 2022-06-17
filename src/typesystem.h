#ifndef TYPESYSTEM_H
#define TYPESYSTEM_H

#include <map>
#include <variant>
#include <vector>
#include <cassert>

namespace TypeSystem
{
    class StructDefinition;

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
        StructDefinition definition() const;
        bool operator==(Type const &other) const;
        bool isIntType() const;
        bool isStructType() const;
        bool isNullType() const;
    };
    
    class StructDefinition
    {
    public:
        struct Field
        {
            std::string name;
            int         offset;
            Type        type;
        };

    private:
        int d_size;
        bool const d_valid{true};
        std::string d_name;
        std::vector<Field> d_fields;

    public:
        StructDefinition(int const sz);
        StructDefinition(std::string const &name);
        StructDefinition():
            d_valid{false}
        {}

        void addField(std::string const &name, Type const &type);
        int size() const;
        std::vector<Field> const &fields() const;
        std::string const &name() const;
    };

    bool add(std::string const &name,
             std::vector<std::pair<std::string, Type>> const &fields);
};

#endif // TYPES_H


