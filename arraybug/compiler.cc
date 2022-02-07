#include "compiler.ih"

int Compiler::lex()
{
    int token = d_scanner.lex();

    switch (token)
    {
    case IDENT:
    case NUM:
        {
            d_val_ = d_scanner.matched();
            break;
        }
    }

    
    
    return token;
}
