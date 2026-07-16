#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "completion.h"
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

void init_shell_signals(void) {
    // Prevent the shell from exiting on Ctrl+C or Ctrl+Z
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    
    // Ignore terminal read/write signals for background jobs
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

// Helper to generate the prompt string dynamically
void get_prompt(char *prompt_buf, size_t max_len) {
      char hostname[256];
      char cwd[4096];
      struct passwd *pw = getpwuid(getuid());

      if (gethostname(hostname, sizeof(hostname)) != 0)
	    strcpy(hostname, "?");

      if (getcwd(cwd, sizeof(cwd)) == NULL)
	    strcpy(cwd, "?");

      snprintf(prompt_buf, max_len, "%s@%s # %s > ",
	    pw ? pw->pw_name : "?",
	    hostname,
	    cwd);
}

int main(void) {
      char *line;
      char prompt[4352];

      init_shell_signals();
      init_completion();

      while (1) {
	    update_jobs();
	    get_prompt(prompt, sizeof(prompt));
        
	    line = readline(prompt);
        
	    if (!line) {
		  // Handle Ctrl+D (EOF) gracefully
		  printf("\n");
		  break;
	    }

	    if (strlen(line) > 0) {
		  add_history(line); // Saves the command in the up/down arrow history buffer!

		  int token_count = 0;
		  LexToken *tokens = lex(line, &token_count);

            if (token_count > 1) { // If not only TOK_EOF
		  int pos = 0;
		  ASTNode *root = parse_sequence(tokens, &pos);
                
		  // Execute AST
		  execute_ast(root);
                
		  free_ast(root);
            }
            
            free_tokens(tokens, token_count);
        }
        
	    free(line); 
      }

      return EXIT_SUCCESS;
}
