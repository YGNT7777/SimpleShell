#ifndef PARSER_H
#define PARSER_H

#include "common.h"

ASTNode *create_node(NodeType type);
ASTNode *parse_sequence(LexToken *tokens, int *pos);
ASTNode *parse_pipe(LexToken *tokens, int *pos);
ASTNode *parse_command(LexToken *tokens, int *pos);
void free_ast(ASTNode *node);
void print_ast(ASTNode *node, int level);

#endif // PARSER_H
