#include "lexer.h"

void add_token(LexToken **tokens, int *count, int *capacity, TokenType type, const char *text, int quoted) {
      if (*count >= *capacity) {
	    *capacity *= 2;
	    *tokens = realloc(*tokens, *capacity * sizeof(LexToken));
	    if (!*tokens) { perror("realloc"); exit(EXIT_FAILURE); }
      }
      (*tokens)[*count].type = type;
      (*tokens)[*count].text = text ? strdup(text) : NULL;
      (*tokens)[*count].quoted = quoted;
      (*count)++;
}

LexToken *lex(char *line, int *count)	{
      int capacity = 32;
      LexToken *tokens = malloc(capacity * sizeof(LexToken));
      if (!tokens) {perror("malloc"); exit(EXIT_FAILURE);}
      
      *count = 0;
      char *p = line;

      while (*p) {
	    if (isspace((unsigned char)*p)) {
		  p++;
		  continue;
	    }

	    else if (*p == '|') {
		  add_token(&tokens, count, &capacity, TOK_PIPE, "|", 0);
		  p++;
		  continue;
	    }

	    else if (*p == '&') {
		  add_token(&tokens, count, &capacity, TOK_BACKGROUND, "&", 0);
		  p++;
		  continue;
	    }

	    else if (*p == ';') {
		  add_token(&tokens, count, &capacity, TOK_SEMI, ";", 0);
		  p++;
		  continue;
	    }

	    else if (*p == '>') {
		  if (*(p + 1) == '>') {
			add_token(&tokens, count, &capacity, TOK_REDIR_APPEND, ">>", 0);
			p += 2;
		  } else {
			add_token(&tokens, count, &capacity, TOK_REDIR_OUT, ">", 0);
			p++;
		  }
		  continue;
	    }

	    else if (*p == '<') {
		  add_token(&tokens, count, &capacity, TOK_REDIR_IN, "<", 0);
		  p++; 
		  continue;
	    }

	    if (*p == '"' || *p == '\'') {
		  char quote = *p;
		  int quoted_type = (quote == '"') ? 2 : 1;
		  p++; // skip opening quote
		  char *start = p;
		  while (*p && *p != quote) p++;
		  
		  if (*p != quote) {
		      fprintf(stderr, "Ssh: unmatched quote\n");
		      break;
		  }
		  size_t len = p - start;
		  char *buf = strndup(start, len);
		  add_token(&tokens, count, &capacity, TOK_WORD, buf, quoted_type);
		  free(buf);
		  p++; // skip closing quote
		  continue;
	      }

	      // (TOK_WORD)
	      char *start = p;
	      while (*p && !isspace((unsigned char)*p) && 
		     *p != '|' && *p != '&' && *p != ';' && *p != '<' && *p != '>') {
		  p++;
	      }
	      size_t len = p - start;
	      char *buf = strndup(start, len);
	      add_token(&tokens, count, &capacity, TOK_WORD, buf, 0);
	      free(buf);
	  }

      add_token(&tokens, count, &capacity, TOK_EOF, NULL, 0);
      return tokens;
}

void free_tokens(LexToken *tokens, int count) {
      for (int i = 0; i < count; i++) {
	    if (tokens[i].text) free(tokens[i].text);
      }
      free(tokens);
}

// --- DEBUGGING ---

const char* token_type_to_string(TokenType type) {
      switch (type) {
	    case TOK_WORD:           return "WORD";
	    case TOK_PIPE:           return "PIPE (|)";
	    case TOK_SEMI:           return "SEMI (;)";
	    case TOK_BACKGROUND:     return "BACKGROUND (&)";
	    case TOK_REDIR_IN:       return "REDIR_IN (<)";
	    case TOK_REDIR_OUT:      return "REDIR_OUT (>)";
	    case TOK_REDIR_APPEND:   return "REDIR_APPEND (>>)";
	    case TOK_EOF:            return "EOF";
	    default:                 return "UNKNOWN";
      }
}

void print_tokens(LexToken *tokens, int count) {
      printf("\n=== LEXER DEBUG OUTPUT ===\n");
      for (int i = 0; i < count; i++) {
	    printf("Token[%d]: Type = %-15s | Quoted = %d | Text = \"%s\"\n",
	    i,
            token_type_to_string(tokens[i].type),
            tokens[i].quoted,
            tokens[i].text ? tokens[i].text : "NULL");
      }
    
      printf("==========================\n\n");
}

