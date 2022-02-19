#ifndef TYPES_H
#define TYPES_H
#include <map>

class TypeSystem
{

public:    
    class Type;
    using FieldDefinition = std::pair<std::string, std::shared_ptr<Type>>;

private:
    
    struct StructDefinition
    {
        struct Field
        {
            std::string name;
            int         offset;
            std::shared_ptr<Type> type;
        };

        std::string name;
        std::vector<Field> fields;

        Struct_(std::string const &nm):
            name(nm)
        {}
        
        void addField(std::string const &name, FieldDefinition const &f)
        {
            int const fieldSize = f.type->size();
            fields.emplace_back(Field{name, d_size, type});
            d_size += fieldSize;
        }

        int size() const
        {
            return d_size;
        }
        
    private:
        int d_size{1};
    };


    static std::map<std::string, StructDefinition> typeMap;

public:
    class Type 
    {
    public:
        virtual int size() const = 0;
    };

    class Int: Type
    {
        int size;
    public:
        Int(int sz): size(sz) {}

        virtual int size() const override
        {
            return size;
        }
    };

    class Struct: Type
    {
        std::string name;
    public:
        Struct(std::string const &nm): name(nm) {}

        virtual int size() const override
        {
            auto const it = TypeSystem::typeMap.find(name);
            if (it == TypeSystem::typeMap.end())
                return -1;

            Struct_ s = TypeSystem::typeMap.at(name);
            return s.size;
        }
    };
b    
    template <typename T, typename Arg>
    std::shared_ptr<Type> type(Arg &&arg)
    {
        return std::make_shared<T>(std::forward<Arg>(arg));
    }

    
    void addType(std::string const &name, std::vector<FieldDefinition> const &fields)
    {
        StructDefinition s(name);
        for (auto const &f: fields)
        {
            std::string const &fieldName = f.first;
            std::shared_ptr<Type> type = f.second;
            s.addField(name, type);
        }
    }

    template <typename T, typename Arg>
    int getSize(Arg &&arg)
    {
        return type<T>(std::forward<Arg>(arg))->size();
    }
};

#endif // TYPES_H


/*

typeSystem.addStruct("S", ...);

allocate("x", TypeSystem::get<Int>(10));
allocate("x", TypeSystem::get<Struct>("S"));

Type::Base &t = memory.type(addr);
typeSystem.getSize(t);



 */
