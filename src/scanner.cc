#include "scanner.ih"


Scanner::Scanner(std::string const &infile, std::string const &outfile):
    ScannerBase(infile, outfile)
{
    //    d_startConditionStack.push(StartCondition_::INITIAL);
}


std::string Scanner::escape(char c)
{
    switch (c)
    {
    case 'n': return std::string(1, '\n');
    case 't': return std::string(1, '\t');
    case '0': return std::string(1, '\0');
    default: return std::string(1, c);
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
