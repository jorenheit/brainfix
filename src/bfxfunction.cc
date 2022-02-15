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

bool Scope::containsFunction(std::string const &name) const
{
    auto const it = std::find_if(d_stack.begin(), d_stack.end(),
                                 [&](std::pair<std::string, StackType<int>> const &item) 
                                 {   
                                     return item.first == name;
                                 });

    return it != d_stack.end();
}

void Scope::pushFunction(std::string const &name)
{
    d_stack.push_back(std::pair<std::string, StackType<int>>(name, StackType<int>{}));
}

std::string Scope::popFunction()
{
    std::string top = current();
    d_stack.pop_back();
    return top;
}

void Scope::push()
{
    static int counter = 0;
    
    auto &subScopeStack = d_stack.back().second;
    subScopeStack.push_back(++counter);
}

std::string Scope::pop()
{
    auto &subScopeStack = d_stack.back().second;
    std::string top = current();
    subScopeStack.pop_back();
    return top;
}

