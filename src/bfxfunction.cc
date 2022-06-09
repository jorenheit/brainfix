#include "bfxfunction.h"

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
    
    for (int scopeId: d_stack.back().second)
        result += std::string("::") + std::to_string(scopeId);

    return result;
}

std::string Scope::enclosing() const
{
    std::string const curr = current();
    return d_stack.back().second.empty() ? "" : curr.substr(0, curr.find_last_of("::") - 1);
}

bool Scope::containsFunction(std::string const &name) const
{
    auto const it = std::find_if(d_stack.begin(), d_stack.end(),
                                 [&](std::pair<std::string, StackType<int>> const &item) 
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

void Scope::push(std::string const &name)
{
    if (name.empty())
    {
        static int counter = 0;
        auto &subScopeStack = d_stack.back().second;
        subScopeStack.push_back(++counter);
    }
    else
    {
        d_stack.push_back(std::pair<std::string, StackType<int>>(name, StackType<int>{}));
        //        d_stack.emplace_back(name, StackType<int>{});
        // TODO: check
    }
}

std::string Scope::pop()
{
    auto &subScopeStack = d_stack.back().second;
    std::string top = current();
    subScopeStack.pop_back();
    return top;
}

