#ifndef TYPESYSTEM_H
#define TYPESYSTEM_H
#include <map>
#include <variant>
#include <vector>
#include <cassert>

class TypeSystem
{
    
public:
    class StructDefinition;
private:
    static std::map<std::string, StructDefinition> typeMap;
public:
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
        Type(std::string const &name):
            d_name(name),
            d_kind(Kind::STRUCT)
        {}

        Type(int const sz):
            d_size(sz),
            d_kind(Kind::INT)
        {}

        Type() = default;

        int size() const;
        std::string name() const;
        bool defined() const;
        StructDefinition definition() const;
        bool operator==(Type const &other) const;
        bool isIntType() const;
        bool isStructType() const;
        bool isNullType() const;
    };
    
private:
    using NameTypePair = std::pair<std::string, Type>;

public:        
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

        void addField(NameTypePair const &f);
        int size() const;
        std::vector<Field> const &fields() const;
        std::string const &name() const;
    };


    static std::string intName(int const sz)
    {
        return "__int_" + std::to_string(sz) + "__";
    }
    
public:
    static bool add(std::string const &name, std::vector<NameTypePair> const &fields);
};

#endif // TYPES_H


