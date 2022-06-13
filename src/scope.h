#ifndef SCOPE_H
#define SCOPE_H

#include <deque>
#include <string>
#include <utility>

class Scope
{
public:
    enum class Type
        {
         Function,
         For,
         While,
         Switch,
         If,
         Anonymous
        };

private:    
    template <typename T>
    using StackType = std::deque<T>;

    struct SubScope
    {
        Type type;
        int id;
    };
    
    StackType<std::pair<std::string, StackType<SubScope>>> d_stack;

public:
    bool empty() const;
    std::string function() const;
    std::string current() const;
    Type currentType() const;
    std::string enclosing() const;
    bool containsFunction(std::string const &name) const;
    void push(Type type);
    void push(std::string const &name);
    std::string popFunction(std::string const &name);
    std::pair<std::string, Type> pop();
};


#endif
