#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>

// --- Builtins Definition ---
extern const char *builtin_str[];
int Ssh_num_builtins(void);

// --- Lexer Types ---
typedef enum {
      TOK_WORD,
      TOK_PIPE,          // |
      TOK_SEMI,          // ;
      TOK_BACKGROUND,    // &
      TOK_REDIR_IN,      // <
      TOK_REDIR_OUT,     // >
      TOK_REDIR_APPEND,  // >>
      TOK_AND,	   	 // &&
      TOK_OR,		 // ||
      TOK_LPAREN,	 // (
      TOK_RPAREN,	 // )
      TOK_EOF
} TokenType;

typedef struct {
      TokenType type;
      char *text;
      int quoted; // 0=none, 1=single, 2=double
} LexToken;

// --- Parser Types (AST) ---
typedef enum {
      NODE_COMMAND,
      NODE_PIPE,
      NODE_SEQUENCE,
      NODE_AND,
      NODE_OR,
      NODE_SUBSHELL
} NodeType;

typedef struct ASTNode {
      NodeType type;
    
      // NODE_COMMAND
      char **args;
      int arg_count;
      char *input_file;
      char *output_file;
      int append;          // 1 if >>, 0 if >
    
      // NODE_PIPE & NODE_SEQUENCE
      struct ASTNode *left;
      struct ASTNode *right;
    
      // Flag background (&)
      int background;
} ASTNode;

// --- Job Control Types ---
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED
} JobState;

typedef struct Job {
    int id;
    pid_t pgid;
    char *command;
    JobState state;
    struct Job *next;
} Job;

extern Job *first_job;

#endif // COMMON_H
