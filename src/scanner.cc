#include "scanner.ih"

std::string Scanner::escape(char c)
{
    switch (c)
    {
    case 'n': return std::string(1, '\n');
    case 't': return std::string(1, '\t');
    default: return std::string(1, c);
    }
}

std::string Scanner::escape(std::string const &matched)
{
    std::string result =
        matched.substr(0, matched.size() - 2) // remove escape sequence from the back
        + escape(matched.back());             // replace by escaped charater

    return result;
}

