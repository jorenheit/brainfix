#include "scope.ih"

bool Scope::empty() const
{
    return d_stack.empty();
}

std::string Scope::function() const
{
    return empty() ? "" : d_stack.back().first;
}

std::string Scope::current() const
{
    std::string result = function();
    if (d_stack.size() == 0)
        return result;
    
    for (SubScope const &sub: d_stack.back().second)
    {
        result += std::string("::") + std::to_string(sub.id);
    }

    return result;
}

Scope::Type Scope::currentType() const
{
    if (d_stack.back().second.empty())
        return Type::Function;
    else
        return d_stack.back().second.back().type;
}

std::string Scope::enclosing() const
{
    std::string const curr = current();
    return d_stack.back().second.empty() ? "" : curr.substr(0, curr.find_last_of("::") - 1);
}

bool Scope::containsFunction(std::string const &name) const
{
    auto const it = std::find_if(d_stack.begin(), d_stack.end(),
                                 [&](auto const &item) 
                                 {   
                                     return item.first == name;
                                 });

    return it != d_stack.end();
}

std::string Scope::popFunction(std::string const &name)
{
    assert(name == function() && "trying to exit function-scope other than current function");
    
    std::string top = current();
    d_stack.pop_back();
    return top;
}

void Scope::push(Type type)
{
    static int counter = 0;
    d_stack.back().second.push_back({
                             .type = type,
                             .id   = ++counter
        });
}

void Scope::push(std::string const &name)
{    
    d_stack.push_back({name, {}});
    //        d_stack.emplace_back(name, StackType<int>{});
    // TODO: check
}

std::pair<std::string, Scope::Type> Scope::pop()
{
    std::string const previousScopeString = current();

    auto &subScopeStack = d_stack.back().second;
    Type const previousScopeType = subScopeStack.back().type;
    subScopeStack.pop_back();

    return {previousScopeString, previousScopeType};
}

