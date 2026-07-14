#ifndef LEXER_H
#define LEXER_H

#include "common.h"

LexToken *lex(char *line, int *count);
void free_tokens(LexToken *tokens, int count);
const char* token_type_to_string(TokenType type);
void print_tokens(LexToken *tokens, int count);

#endif // LEXER_H
