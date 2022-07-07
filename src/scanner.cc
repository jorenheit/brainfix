#include "scanner.ih"


Scanner::Scanner(std::string const &infile, std::string const &outfile):
    ScannerBase(infile, outfile)
{
    //    d_startConditionStack.push(StartCondition_::INITIAL);
}


char Scanner::escape(char c)
{
    std::cerr << "Escape: " << c << '\n';
    
    switch (c)
    {
    case 'n': return '\n';
    case 't': return '\t';
    case '0': return '\0';
    default: return c;
    }
}

char Scanner::escapeTestContent(std::string const &str)
{
    try{
        return static_cast<char>(std::stoi(str));
    } catch(std::invalid_argument const &)
    {
        std::cerr << "ERROR: escape sequence does not contain a number.\n";
        std::exit(1);
    }
}

void Scanner::pushStartCondition(StartCondition_ next)
{
    d_startConditionStack.push(startCondition());
    begin(next);
}

void Scanner::popStartCondition()
{
    begin(d_startConditionStack.top());
    d_startConditionStack.pop();
}
