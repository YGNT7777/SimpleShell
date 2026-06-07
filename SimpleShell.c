#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>

#define SSH_TOK_BUFSIZE 64
#define SSH_TOK_DELIM " \t\r\n\a"

typedef struct {
    char *text;
    int quoted;        // 0 = none, 1 = single, 2 = double
} Token;

typedef struct {
    char *input_file;
    char *output_file;
    int append;
} Redirection;

typedef struct {
    char *cmd;
    int background;
} Command;

/*
  Function Declarations for builtin shell commands:
 */
int Ssh_cd(char **args);
int Ssh_help(char **args);
int Ssh_exit(char **args);

/*
  List of builtin commands, followed by their corresponding functions.
 */
char *builtin_str[] = {
      "cd",
      "help",
      "exit"
};

int (*builtin_func[]) (char **) = {
      &Ssh_cd,
      &Ssh_help,
      &Ssh_exit
};

int Ssh_num_builtins() {
      return sizeof(builtin_str) / sizeof(char *);
}

/*
  Builtin function implementations.
*/
int Ssh_cd(char **args) {
      char  *target;
      if (args[1] == NULL) {
	    target = getenv("HOME");
	    
	    if (target == NULL) {
		  fprintf(stderr, "Ssh: HOME not set \"cd\"\n");
		  return 1;
	    }
      } 
      else {
	    target = args[1];
      }

      if (chdir(target) != 0) {
	    perror("Ssh: cd");
      }
      printf("[debug] changing dir to: %s\n", target);
      return 1;
}

int Ssh_help(char **args) {
      int i;
      printf("Ssh\n");
      printf("Type program names and arguments, and hit enter.\n");
      printf("The following are built in:\n");

      for (i = 0; i < Ssh_num_builtins(); i++) {
	    printf("  %s\n", builtin_str[i]);
      }

      printf("Use the man command for information on other programs.\n");
      return 1;
}

int Ssh_exit(char **args) {
      return 0;
}

void Ssh_print_prompt(void) {
    char hostname[256];
    char cwd[4096];

    struct passwd *pw = getpwuid(getuid());

    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "?");

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, "?");

    printf("%s@%s # %s > ",
           pw ? pw->pw_name : "?",
           hostname,
           cwd);

    fflush(stdout);
}


char *Ssh_read_line(void) {
      char *line = NULL;
      size_t bufsize = 0;

      if (getline(&line, &bufsize, stdin) == -1){
	    if (feof(stdin)) {
		  exit(EXIT_SUCCESS);  
	    } else  {
		  perror("readline");
		  exit(EXIT_FAILURE);
	    }
      }

      return line;
}

void Ssh_free_args(char **args) {
    int i = 0;

    while (args[i] != NULL) {
        free(args[i]);
        i++;
    }

    free(args);
}

void Ssh_free_tokens(Token **tokens) {
    int i = 0;
    while (tokens[i]) {
        free(tokens[i]->text);
        free(tokens[i]);
        i++;
    }
    free(tokens);
}

Token **Ssh_split_line(char *line, int *count) {
    int bufsize = SSH_TOK_BUFSIZE;
    int position = 0;

    Token **tokens = malloc(bufsize * sizeof(Token *));
    if (!tokens) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    char *p = line;

    while (*p) {
      /* Skip leading whitespace */
      while (*p == ' ' || *p == '\t' || *p == '\n')
	    p++;

      if (*p == '\0') break;

      Token *token = malloc(sizeof(Token));
      if (!token) {
	    perror("malloc");
	    exit(EXIT_FAILURE);
      }

      char *start;

        /* Quoted string */
        if (*p == '"' || *p == '\'') {

            char quote = *p;
	    int quoted_type = (quote == '"') ? 2 : 1;

            p++;                // skip opening quote
            start = p;

            while (*p && *p != quote)
                p++;

	    if (*p != quote) {
		  fprintf(stderr, "Ssh: unmatched quote\n");
		  free(token);
		  break;
	    }
	    
	    size_t len = p - start;
	    
	    token->text = strndup(start, len);
	    token->quoted = quoted_type;

	    p++; // Skip clossing quote
        }
        else {

            start = p;

            while (*p &&
                   *p != ' ' &&
                   *p != '\t' &&
                   *p != '\n')
            {
                p++;
            }

            size_t len = p - start;

	    token->text = strndup(start, len);
	    token->quoted = 0;
        }

        tokens[position++] = token;

        if (position >= bufsize) {
            bufsize += SSH_TOK_BUFSIZE;

	    Token **tmp = realloc(tokens, bufsize * sizeof(Token *));
            if (!tmp) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }

            tokens = tmp;
        }
    }

    tokens[position] = NULL;
    return tokens;
}

int Ssh_parse_redirection(char **args, Redirection *redir) {
      redir->input_file = NULL;
      redir->output_file = NULL;
      redir->append = 0;

      int dst = 0;

      for (int src = 0; args[src] != NULL; src++) {
	    if (strcmp(args[src], "<") == 0) {
		  if (args[src + 1] == NULL) {
		      fprintf(stderr, "Ssh: expected filename after <\n");
		      return -1;
		  }

		  redir->input_file = args[++src];
	      }

	    else if (strcmp(args[src], ">") == 0) {
		  if (args[src + 1] == NULL) {
		      fprintf(stderr, "Ssh: expected filename after >\n");
		      return -1;
		  }

		  redir->output_file = args[++src];
		  redir->append = 0;
	      }

	    else if (strcmp(args[src], ">>") == 0) {
		  if (args[src + 1] == NULL) {
		      fprintf(stderr, "Ssh: expected filename after >>\n");
		      return -1;
		  }

		  redir->output_file = args[++src];
		  redir->append = 1;
	      }

	    else {
		  args[dst++] = args[src];
	    }
	  }
	  args[dst] = NULL;
	  return 0;
}

Command *Ssh_split_commands(char *line, int *count) {
      int bufsize = 16;
      int position = 0;

      Command *cmds = malloc(bufsize * sizeof(Command));
      if (!cmds) {
	    perror("malloc");
	    exit(EXIT_FAILURE);
      }

      char *start = line;

      for (char *p = line; ; p++) {
	    if (*p == '&' || *p == ';' || *p == '\0') {
		  char op = *p;
		  *p = '\0';

		  // Trim leading whitespaces
		  while (*start == ' ' || *start == '\t')
		      start++;

		  // Trim trailing spaces
		  char *end = start + strlen(start);
		  while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t'))
		      *(--end) = '\0';
		  
		  // Check capacity before writing
		  if (position >= bufsize) {
		      bufsize *= 2;
		      cmds = realloc(cmds, bufsize * sizeof(Command));
		      if (!cmds) {
			  perror("realloc");
			  exit(EXIT_FAILURE);
		      }
		  }

		  if (*start != '\0') {
			cmds[position].cmd = strdup(start);
			// Set background mode
			cmds[position].background = (op == '&');
			position++;
		  }

		  start = p + 1;
		  if (op == '\0')
		      break;

	      }
      }
      *count = position;
      return cmds;
}

char *expand_token(char *token, int quoted) {

    if (quoted == 1) {
        return strdup(token);
    }

    char buffer[4096];
    int bi = 0;

    for (int i = 0; token[i] != '\0'; i++) {
	    if (token[i] == '\\' && token[i + 1] != '\0') {
		  buffer[bi++] = token[++i];
		  continue;
	    }

	    if (token[i] == '$' && quoted != 1) {
		  char var[128];
		  int vi = 0;
		  
		  // ${VAR} syntax
		  if (token[i+1] == '{') {
			i += 2; // skip '$' and '{'
			
			while (token[i] && token[i] != '}' && vi < (int)sizeof(var) - 1 ) {
			      var[vi++] = token[i++];
			}
			
			var[vi] = '\0';
			
			if (token[i] != '}') {
			      fprintf(stderr, "Ssh: unmatched variable brace\n");
			      return strdup(token);
			}
		  }
		  else {
			i++; // skips '$'
			
			while (token[i] && 
			      (isalnum((unsigned char)token[i]) || 
			      token[i] == '_') && 
			      vi < (int)sizeof(var) - 1) {
			      var[vi++] = token[i++];
			}
			      
			var[vi] = '\0';
			i--;
		  }
		  char *val = getenv(var);
	    
		  if (val) {
			for (int k = 0; val[k] && bi < (int)sizeof(buffer) - 1; k++) {
			      buffer[bi++] = val[k];
			}
		  }
	    }
	    else {
		  buffer[bi++] = token[i];
	    }
      }
      buffer[bi] = '\0';
      return strdup(buffer);
}

char **Ssh_expand_args(Token **args) {
      int i = 0;

      char **out = malloc(64 * sizeof(char*));

      while (args[i] != NULL) {
	    out[i] = expand_token(args[i]->text, args[i]->quoted);
	    i++;
      }
      
      out[i] = NULL;
      
      return out;
}

int Ssh_launch(char **args, int background) {
      pid_t pid, wpid;
      int status;

      Redirection redir;

      if (Ssh_parse_redirection(args, &redir) != 0)
	  return 1;

      pid = fork();
      if (pid == 0) {
	    // Child process
	    
	    // Create input file or output file
	    if (redir.input_file) {

		FILE *f = fopen(redir.input_file, "r");

		if (!f) {
		    perror("Ssh");
		    exit(EXIT_FAILURE);
		}

		dup2(fileno(f), STDIN_FILENO);
		fclose(f);
	    }

	    if (redir.output_file) {

		FILE *f = fopen(
		    redir.output_file,
		    redir.append ? "a" : "w"
		);

		if (!f) {
		    perror("Ssh");
		    exit(EXIT_FAILURE);
		}

		dup2(fileno(f), STDOUT_FILENO);
		fclose(f);
	    }

	    if (execvp(args[0], args) == -1) {
		  perror("Ssh");
	    }
	    exit(EXIT_FAILURE);

      } else if (pid < 0) {
	    // Error forking
	    perror("Ssh");
      } else {
	    // Parent process
	    if (!background) {
		  do {
			wpid = waitpid(pid, &status, WUNTRACED);
		  } while (!WIFEXITED(status) && !WIFSIGNALED(status));
	    }
      }

      return 1;
}

int Ssh_execute(Token **tokens, int background) {

      if (!tokens || !tokens[0]) {
	    return 1;
      }

      char **args =  Ssh_expand_args(tokens);
      if (args == NULL) {
	    return 1;
      }

      int result;
      for (int i = 0; i < Ssh_num_builtins(); i++) {
	    if (strcmp(args[0], builtin_str[i]) == 0) {
		  result = (*builtin_func[i])(args);
		  Ssh_free_args(args);
		  return result;
	    }
      }

      result = Ssh_launch(args, background);
      Ssh_free_args(args);

      return result;
}

void Ssh_loop(void) {
      char *line;
      int status = 1;

      while (1) {
	    Ssh_print_prompt();
	    line = Ssh_read_line();

	    int cmd_count = 0;
	    Command *cmds = Ssh_split_commands(line, &cmd_count);

	    for (int i = 0; i < cmd_count; i++) {
		  int token_count = 0;
		  Token **tokens = Ssh_split_line(cmds[i].cmd, &token_count);
		  status = Ssh_execute(tokens, cmds[i].background);

		  Ssh_free_tokens(tokens);
		  free(cmds[i].cmd);

		  if (status == 0)
			break;
		  }

		  free(cmds);
		  free(line);
	    

		  if (status == 0)
			break;
	    }
}

int main(int argc, char **argv) {
      Ssh_loop();

      return EXIT_SUCCESS;
}
