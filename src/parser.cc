#include "parser.ih"

Parser::Parser(std::string const &filename, Compiler &comp):
    d_scanner(filename, "scannerlog.txt"),
    d_comp(comp)
{}

int Parser::lex()
{
    int token = d_scanner.lex();

    switch (token)
    {
    case IDENT:
    case STR:
        {
            d_val_.assign<Tag_::STRING>(d_scanner.matched());
            break;
        }
    case NUM:
        {
            d_val_.assign<Tag_::INT8>(static_cast<uint8_t>(std::stoi(d_scanner.matched())));
            break;
        }
    case CHR:
        {
            d_val_.assign<Tag_::CHAR>(d_scanner.matched()[0]);
            break;
        }
    default:
        break;
    }

    return token;
}

void Parser::error()
{
    std::cerr << "ERROR: Syntax error on line "
              << d_scanner.lineNr() << " of file "
              << d_scanner.filename() << '\n';
}


