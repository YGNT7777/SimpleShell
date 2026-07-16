#include "parser.h"
#include <stdlib.h>

// Helper to expand environment variables like $USER to their actual values
static char *expand_env_var(const char *arg) {
      const char *start = arg;
      int has_quotes = 0;

      // Check if the argument starts with a double quote: e.g. "$USER"
      if (arg[0] == '"') {
	    start = arg + 1;
	    has_quotes = 1;
      }

      // Check if the actual payload starts with '$'
      if (start[0] != '$') {
	    return strdup(arg); // Standard string, return as-is
      }

      // Extract the variable name
      const char *var_name = start + 1;
      char *var_name_dup = strdup(var_name);
      if (!var_name_dup) { perror("strdup"); exit(EXIT_FAILURE); }

      // Clean up the trailing double quote if it exists
      size_t len = strlen(var_name_dup);
      if (has_quotes && len > 0 && var_name_dup[len - 1] == '"') {
	    var_name_dup[len - 1] = '\0';
      }

      // Retrieve the environment variable value
      char *val = getenv(var_name_dup);
      free(var_name_dup); 

      return val ? strdup(val) : strdup("");
}

ASTNode *create_node(NodeType type) {
      ASTNode *node = calloc(1, sizeof(ASTNode));
      if (!node) { perror("calloc"); exit(EXIT_FAILURE); }
      node->type = type;
      return node;
}

ASTNode *parse_sequence(LexToken *tokens, int *pos);
ASTNode *parse_pipe(LexToken *tokens, int *pos);
ASTNode *parse_command(LexToken *tokens, int *pos);
ASTNode *parse_logical(LexToken *tokens, int *pos);

ASTNode *parse_logical(LexToken *tokens, int *pos) {
    ASTNode *left = parse_pipe(tokens, pos);

    while (tokens[*pos].type == TOK_AND || tokens[*pos].type == TOK_OR) {
        TokenType op_type = tokens[*pos].type;
        (*pos)++; // Consume the '&&' or '||'

        ASTNode *parent = create_node(op_type == TOK_AND ? NODE_AND : NODE_OR);
        parent->left = left;
        parent->right = parse_pipe(tokens, pos);
        left = parent;
    }

    return left;
}

//  ; and &
ASTNode *parse_sequence(LexToken *tokens, int *pos) {
      ASTNode *left = parse_logical(tokens, pos);
    
      while (tokens[*pos].type == TOK_SEMI || tokens[*pos].type == TOK_BACKGROUND) {
	      TokenType op = tokens[*pos].type;
	      (*pos)++; // Skip ; / &
	      
	      ASTNode *node = create_node(NODE_SEQUENCE);
	      node->left = left;
	      
	      if (op == TOK_BACKGROUND) {
		  left->background = 1; // execute in background
	      }
	      
	      if (tokens[*pos].type != TOK_EOF) {
		  node->right = parse_sequence(tokens, pos);
	      }
	      left = node;
      }
	  
      return left;
}

//  |
ASTNode *parse_pipe(LexToken *tokens, int *pos) {
      ASTNode *left = parse_command(tokens, pos);
    
      while (tokens[*pos].type == TOK_PIPE) {
	    (*pos)++; // Προσπέραση του |
	    ASTNode *node = create_node(NODE_PIPE);
	    node->left = left;
	    node->right = parse_pipe(tokens, pos);
	    left = node;
      }
	  
      return left;
}

// <, >, >>
ASTNode *parse_command(LexToken *tokens, int *pos) {
      ASTNode *node = create_node(NODE_COMMAND);
      int cap = 8;
      node->args = malloc(cap * sizeof(char *));
      if (!node->args) { perror("malloc"); exit(EXIT_FAILURE); }
    
      while (tokens[*pos].type == TOK_WORD || tokens[*pos].type == TOK_REDIR_IN || 
	    tokens[*pos].type == TOK_REDIR_OUT || tokens[*pos].type == TOK_REDIR_APPEND) {
	    
	    TokenType t = tokens[*pos].type;
        
	    if (t == TOK_WORD) {
		  if (node->arg_count >= cap - 1) {
		  cap *= 2;
		  node->args = realloc(node->args, cap * sizeof(char *));
		  if (!node->args) { perror("realloc"); exit(EXIT_FAILURE); }
	    }
            node->args[node->arg_count++] = expand_env_var(tokens[*pos].text);
            (*pos)++;
        } else if (t == TOK_REDIR_IN) {
	    (*pos)++;
            if (tokens[*pos].type != TOK_WORD) {
		  fprintf(stderr, "Ssh: syntax error near unexpected token '<'\n");
		  return node;
            }
            node->input_file = expand_env_var(tokens[*pos].text);
            (*pos)++;
        } else if (t == TOK_REDIR_OUT || t == TOK_REDIR_APPEND) {
	    node->append = (t == TOK_REDIR_APPEND);
            (*pos)++;
            if (tokens[*pos].type != TOK_WORD) {
		  fprintf(stderr, "Ssh: syntax error near unexpected token '>' or '>>'\n");
		  return node;
            }
            node->output_file = expand_env_var(tokens[*pos].text);
            (*pos)++;
        }
      }
      node->args[node->arg_count] = NULL; // NULL-terminated for execvp 
      return node;
}

void free_ast(ASTNode *node) {
      if (!node) return;
      if (node->type == NODE_COMMAND) {
	    for (int i = 0; i < node->arg_count; i++) {
		  free(node->args[i]);
	    }
	    free(node->args);
	    if (node->input_file) free(node->input_file);
	    if (node->output_file) free(node->output_file);
	  } else {
	    free_ast(node->left);
	    free_ast(node->right);
	  }
      free(node);
}

// --- DEBUGGING ---
void print_ast(ASTNode *node, int level) {
      if (!node) return;

      for (int i = 0; i < level; i++) printf("  ");

      if (node->type == NODE_COMMAND) {
	    printf("COMMAND: ");
	    for (int i = 0; i < node->arg_count; i++) {
		  printf("[%s] ", node->args[i]);
	    }
	    if (node->input_file) printf("< %s ", node->input_file);
	    if (node->output_file) printf("%s %s ", node->append ? ">>" : ">", node->output_file);
	    if (node->background) printf("&");
	    printf("\n");
      } else if (node->type == NODE_PIPE) {
	    printf("PIPE (|)\n");
	    print_ast(node->left, level + 1);
	    print_ast(node->right, level + 1);
      } else if (node->type == NODE_SEQUENCE) {
	    printf("SEQUENCE (; / &)\n");
	    print_ast(node->left, level + 1);
	    if (node->right) print_ast(node->right, level + 1);
      } else if (node->type == NODE_AND) { 
	    printf("AND (&&)\n");
	    print_ast(node->left, level + 1);
	    print_ast(node->right, level + 1);
      } else if (node->type == NODE_OR) {  
	    printf("OR (||)\n");
	    print_ast(node->left, level + 1);
	    print_ast(node->right, level + 1);
      }
}
