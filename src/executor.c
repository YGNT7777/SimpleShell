#include "executor.h"

const char *builtin_str[] = {
      "cd",
      "help",
      "exit"
};

int Ssh_num_builtins(void) {
      return sizeof(builtin_str) / sizeof(char *);
}

int execute_ast(ASTNode *node) {
      if (!node) return 1;

      // SEQUENCE (; or &)
      if (node->type == NODE_SEQUENCE) {
	    int status = execute_ast(node->left);
	    if (node->right) {
		  status = execute_ast(node->right);
	    }
	    return status;
      }

      // PIPE (|)
      if (node->type == NODE_PIPE) {
	    int pipefd[2];
	    if (pipe(pipefd) == -1) { 
		  perror("Ssh: pipe"); 
		  return 1; 
	    }

	    pid_t pid1 = fork();
	    if (pid1 == 0) {
		  // Child 1 Write pipe
		  dup2(pipefd[1], STDOUT_FILENO);
		  close(pipefd[0]); 
		  close(pipefd[1]);
		  execute_ast(node->left);
		  exit(EXIT_FAILURE);
	    }

	    pid_t pid2 = fork();
	    if (pid2 == 0) {
		  // Child 2 Read pipe
		  dup2(pipefd[0], STDIN_FILENO);
		  close(pipefd[0]); 
		  close(pipefd[1]);
		  execute_ast(node->right);
		  exit(EXIT_FAILURE);
	      }

	      //  Parent closes pipes and waits for childrens 
	      close(pipefd[0]); 
	      close(pipefd[1]);
	      waitpid(pid1, NULL, 0);
	      waitpid(pid2, NULL, 0);
	      return 1;
      }

      // COMMAND
      if (node->type == NODE_COMMAND) {
	    if (node->arg_count == 0) return 1;
	    // --- BUILTINS ---
	    if (strcmp(node->args[0], "exit") == 0) {
		  return 0;
	    }

	    if (strcmp(node->args[0], "cd") == 0) {
		  char *target = node->args[1] ? node->args[1] : getenv("HOME");
		  if (!target) {
			fprintf(stderr, "Ssh: HOME not set\n");
		  } else if (chdir(target) != 0) {
			perror("Ssh: cd");
		  }
		  return 1;
	    }

	    if (strcmp(node->args[0], "help") == 0) {
		  printf("SimpleShell\n");
		  printf("Type program names and arguments, and hit enter.\n");
		  printf("The following are built in:\n");

		  for (int i = 0; i < Ssh_num_builtins(); i++) {
		      printf("  %s\n", builtin_str[i]);
		  }

		  printf("Use the man command for information on other programs.\n");
		  return 1;
	    }
        // --- EXTERNAL COMMAND EXECUTION & REDIRECTION ---

        pid_t pid = fork();
        if (pid == 0) {
	    // Child Process: Redirections
            if (node->input_file) {
		  int fd = open(node->input_file, O_RDONLY);
		  if (fd < 0) { perror("Ssh: open input"); exit(EXIT_FAILURE); }
		  dup2(fd, STDIN_FILENO);
		  close(fd);
            }
            
            if (node->output_file) {
		  int flags = O_WRONLY | O_CREAT | (node->append ? O_APPEND : O_TRUNC);
		  int fd = open(node->output_file, flags, 0644);
		  if (fd < 0) { perror("Ssh: open output"); exit(EXIT_FAILURE); }
		  dup2(fd, STDOUT_FILENO);
		  close(fd);
            }

            if (execvp(node->args[0], node->args) == -1) {
		  perror("Ssh");
            }

            exit(EXIT_FAILURE);
        } else if (pid < 0) {
	    perror("Ssh: fork");
        } else {
            // Parent Process
            if (!node->background) {
		  waitpid(pid, NULL, 0);
            } else {
		  printf("[Process running in background with PID %d]\n", pid);
            }
        }

        return 1;
    }

    return 1;
}

