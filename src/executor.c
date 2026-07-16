#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <unistd.h>
#include <sys/wait.h>

static int in_subshell = 0;

const char *builtin_str[] = {
      "cd",
      "help",
      "exit",
      "jobs",
      "fg",
      "bg",
      "kill"
};

Job *first_job = NULL;
int next_job_id = 1;

void add_job(pid_t pgid, JobState state, const char *command) {
      Job *new_job = malloc(sizeof(Job));
      new_job->id = next_job_id++;
      new_job->pgid = pgid;
      new_job->command = strdup(command);
      new_job->state = state;
      new_job->next = first_job;
      first_job = new_job;
}

int get_job_id_by_pid(pid_t pgid) {
      Job *j = first_job;
      while (j) {
	    if (j->pgid == pgid) return j->id;
	    j = j->next;
      }
      return 0;
}

// Clean exited background jobs from list
void update_jobs(void) {
      Job *curr = first_job;
      Job *prev = NULL;
      while (curr) {
	    int status;
	    // WNOHANG checks process status without pausing execution
	    pid_t res = waitpid(-curr->pgid, &status, WNOHANG | WUNTRACED);
        
	    int remove = 0;
	    if (res > 0) {
		  if (WIFEXITED(status) || WIFSIGNALED(status)) {
			remove = 1;
		  } else if (WIFSTOPPED(status)) {
			curr->state = JOB_STOPPED;
		  } else if (WIFCONTINUED(status)) {
		  curr->state = JOB_RUNNING;
		  }
	    }

	    if (remove) {
		  Job *temp = curr;
		  if (prev) prev->next = curr->next;
		  else first_job = curr->next;
            
		  curr = curr->next;
		  free(temp->command);
		  free(temp);
	    } else {
		  prev = curr;
		  curr = curr->next;
	    }
      }
}

int Ssh_num_builtins(void) {
      return sizeof(builtin_str) / sizeof(char *);
}

// Helper to find $( and extract the command inside.
// Returns a dynamically allocated string of the inner command, or NULL if not found.
char *extract_command_substitution(const char *arg, int *start_idx, int *end_idx) {
      const char *start = strstr(arg, "$(");
      if (!start) return NULL;

      *start_idx = start - arg;
    
      // Find the matching closing parenthesis
      int paren_depth = 1;
      const char *p = start + 2;
      while (*p && paren_depth > 0) {
	    if (*p == '(') paren_depth++;
	    else if (*p == ')') paren_depth--;
	    if (paren_depth == 0) break;
	    p++;
      }

      if (paren_depth != 0 || *p != ')') {
	    fprintf(stderr, "Ssh: syntax error: unmatched ')' in command substitution\n");
	    return NULL;
      }

      *end_idx = p - arg;

      // Allocate and copy the inner command
      int len = *end_idx - (*start_idx + 2);
      char *inner_cmd = malloc(len + 1);
      strncpy(inner_cmd, start + 2, len);
      inner_cmd[len] = '\0';

      return inner_cmd;
}

#define BUFFER_SIZE 4096

// Executes an inner command and returns its stdout as a dynamically allocated string.
char *capture_command_output(const char *cmd_str) {
      int pipefd[2];
      if (pipe(pipefd) == -1) {
	    perror("Ssh: pipe failed");
	    return strdup("");
      }

      pid_t pid = fork();
      if (pid < 0) {
	    perror("Ssh: fork failed");
	    close(pipefd[0]);
	    close(pipefd[1]);
	    return strdup("");
      }

      if (pid == 0) {
	    // --- CHILD PROCESS ---
	    // Redirect stdout to the pipe
	    dup2(pipefd[1], STDOUT_FILENO);
	    close(pipefd[0]);
	    close(pipefd[1]);

	    in_subshell = 1; // Flagging that we inside subshell
	    // Make a writable copy of the command string for the lexer
	    char *writable_cmd = strdup(cmd_str);

	    int count = 0;
	    LexToken *tokens = lex(writable_cmd, &count);
	    int pos = 0;
	    ASTNode *root = parse_sequence(tokens, &pos);

	    if (root) {
		  // fprintf(stderr, "[DEBUG Subshell] Executing parsed AST for: %s\n", cmd_str);
		  execute_ast(root);
		  free_ast(root);
	    }

	    if (tokens) free(tokens);
	    free(writable_cmd);
	    exit(0); 
      } 
    
      // --- PARENT PROCESS ---
      close(pipefd[1]); // Parent doesn't write

      // Read the output from the pipe
      char buffer[BUFFER_SIZE];
      size_t capacity = BUFFER_SIZE;
      char *output = malloc(capacity);
      size_t total_bytes = 0;
      ssize_t bytes_read;

      while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
	    // fprintf(stderr, "[DEBUG Parent] Read %ld bytes from subshell\n", (long)bytes_read);
	    if (total_bytes + bytes_read >= capacity) {
		  capacity *= 2;
		  output = realloc(output, capacity);
	    }
	    memcpy(output + total_bytes, buffer, bytes_read);
	    total_bytes += bytes_read;
      }
      // fprintf(stderr, "[DEBUG Parent] Total bytes captured: %ld\n", (long)total_bytes);
      close(pipefd[0]);
      waitpid(pid, NULL, 0);

      output[total_bytes] = '\0';

      // Strip any trailing newlines (standard shell behavior)
      while (total_bytes > 0 && (output[total_bytes - 1] == '\n' || output[total_bytes - 1] == '\r')) {
	    output[total_bytes - 1] = '\0';
	    total_bytes--;
      }

      return output;
}

void expand_command_substitution(ASTNode *node) {
      for (int i = 0; i < node->arg_count; i++) {
	    // fprintf(stderr, "[DEBUG Expand] Checking arg[%d]: '%s'\n", i, node->args[i]);
	    int start_idx, end_idx;
	    char *inner_cmd = extract_command_substitution(node->args[i], &start_idx, &end_idx);
        
	    if (inner_cmd) {
		  // Capture the stdout of the inner command
		  char *captured_output = capture_command_output(inner_cmd);

		  // Reconstruct the argument string with the substitution replaced
		  int prefix_len = start_idx;
		  int suffix_len = strlen(node->args[i]) - end_idx - 1;
		  int new_len = prefix_len + strlen(captured_output) + suffix_len;

		  char *new_arg = malloc(new_len + 1);
		  
		  // Copy prefix (before '$(')
		  strncpy(new_arg, node->args[i], prefix_len);
		  new_arg[prefix_len] = '\0';
		  
		  // Concatenate captured output
		  strcat(new_arg, captured_output);
		  
		  // Concatenate suffix (after ')')
		  strcat(new_arg, node->args[i] + end_idx + 1);

		  // Free the old argument and swap in the new expanded one
		  free(node->args[i]);
		  node->args[i] = new_arg;

		  free(inner_cmd);
		  free(captured_output);
		  
		  // Recursively check this argument again in case there are multiple $(...) in one arg
		  i--; 
	      }
	  }
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
      
      // AND
      if (node->type == NODE_AND) {
	    int left_status = execute_ast(node->left);
	    if (left_status == 0) {
		  return execute_ast(node->right);
	    }
	    return left_status;
      }

      // OR
      if (node->type == NODE_OR) {
	    int left_status = execute_ast(node->left);
	    if (left_status != 0) {
		  return execute_ast(node->right);
	    }
	    return execute_ast(node->left);
      }

      // COMMAND
      if (node->type == NODE_COMMAND) {
	    if (node->arg_count == 0) return 0;

	    expand_command_substitution(node);

	    // --- BUILTINS ---
	    if (strcmp(node->args[0], "exit") == 0) {
		  exit(0);
	    }

	    if (strcmp(node->args[0], "cd") == 0) {
		  char *target = node->args[1] ? node->args[1] : getenv("HOME");
		  if (!target) {
			fprintf(stderr, "Ssh: HOME not set\n");
			return 1;
		  } else if (chdir(target) != 0) {
			perror("Ssh: cd");
			return 1;
		  }
		  return 0;
	    }

	    if (strcmp(node->args[0], "help") == 0) {
		  printf("SimpleShell\n");
		  printf("Type program names and arguments, and hit enter.\n");
		  printf("The following are built in:\n");

		  for (int i = 0; i < Ssh_num_builtins(); i++) {
		      printf("  %s\n", builtin_str[i]);
		  }

		  printf("Use the man command for information on other programs.\n");
		  return 0;
	    }
	    
	    if (strcmp(node->args[0], "jobs") == 0) {
		  update_jobs(); // Clean up jobs first
		  Job *j = first_job;
		  if (!j) printf("No current background jobs.\n");
		  while (j) {
		      printf("[%d]  %s          %s &\n", 
			     j->id, 
			     j->state == JOB_RUNNING ? "Running" : "Stopped", 
			     j->command);
		      j = j->next;
		  }
		  return 0;
	    }
	    
	    if (strcmp(node->args[0], "fg") == 0) {
		  if (!node->args[1]) {
		      fprintf(stderr, "usage: fg <job_id>\n");
		      return 1;
		  }
		  int target_id = atoi(node->args[1]);
		  Job *j = first_job;
		  while (j && j->id != target_id) j = j->next;

		  if (j) {
		      printf("%s\n", j->command);
		      // Give process group control of the terminal
		      tcsetpgrp(STDIN_FILENO, j->pgid);
		      
		      // Send signal to resume process in case it was stopped
		      kill(-j->pgid, SIGCONT);
		      
		      // Wait for it in foreground
		      int status;
		      waitpid(-j->pgid, &status, WUNTRACED);
		      
		      if (WIFSTOPPED(status)) {
			  j->state = JOB_STOPPED;
		      } else {
			  // Remove from list if finished
			  update_jobs();
		      }
		      
		      // Reclaim terminal control
		      tcsetpgrp(STDIN_FILENO, getpgrp());
		  } else {
			fprintf(stderr, "Ssh: fg: no such job\n");
			return 1;
		  }
		  return 0;
	    }

	    if (strcmp(node->args[0], "bg") == 0) {
		  if (!node->args[1]) {
		      fprintf(stderr, "usage: bg <job_id>\n");
		      return 1;
		  }
		  int target_id = atoi(node->args[1]);
		  Job *j = first_job;
		  while (j && j->id != target_id) j = j->next;

		  if (j) {
		      j->state = JOB_RUNNING;
		      printf("[%d] %s &\n", j->id, j->command);
		      // Send resume signal in background
		      kill(-j->pgid, SIGCONT);
		  } else {
			fprintf(stderr, "Ssh: bg: no such job\n");
			return 1;
		  }
		  return 0;
	    }
	    
	    if (strcmp(node->args[0], "kill") == 0) {
		  if (!node->args[1]) {
		      fprintf(stderr, "usage: kill <job_id>\n");
		      return 1;
		  }
		  int target_id = atoi(node->args[1]);
		  Job *j = first_job;
		  while (j && j->id != target_id) j = j->next;

		  if (j) {
		      // Send the SIGTERM signal to the entire process group
		      // Passing the negative pgid (-j->pgid) sends the signal to all processes in that group
		      if (kill(-j->pgid, SIGTERM) == 0) {
			  printf("Sent SIGTERM to job [%d] (PGID %d)\n", j->id, j->pgid);
			  
			  // If the job was suspended, send SIGCONT to force it to wake up and process the SIGTERM
			  if (j->state == JOB_STOPPED) {
			      kill(-j->pgid, SIGCONT);
			  }
		      } else {
			perror("Ssh: kill");
			return 1;
		      }
		  } else {
			fprintf(stderr, "Ssh: kill: no such job\n");
			return 1;
		  }
		  return 0;
	      }
      // --- EXTERNAL COMMAND EXECUTION & REDIRECTION ---
      pid_t pid = fork();
        if (pid == 0) {
            // --- CHILD PROCESS ---
            
	    // Establish process group and terminal control if NOT in a subshell
	    if (!in_subshell) {
		  pid_t pgid = getpid();
		  setpgid(0, pgid);
		  // ONLY claim terminal control if STDIN is actually a terminal (tty)
		  if (!node->background && isatty(STDIN_FILENO)) {
			tcsetpgrp(STDIN_FILENO, pgid);
		  }
	    }
            
            // Restore default signal handlers so the child CAN be killed/suspended
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

            // Redirection logic (<, >, >>) ...
	    // <
	    if (node->input_file) {
                int fd_in = open(node->input_file, O_RDONLY);
                if (fd_in < 0) {
                    perror("Ssh: open input file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            }

            // (> and >>)
            if (node->output_file) {
                int flags = O_WRONLY | O_CREAT;
                if (node->append) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }
                
                // Open with read/write permissions for owner, read-only for group/others
                int fd_out = open(node->output_file, flags, 0644);
                if (fd_out < 0) {
                    perror("Ssh: open output file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }

	    //fprintf(stderr, "[DEBUG Grandchild] Executing: %s with in_subshell=%d\n", node->args[0], in_subshell);
            if (execvp(node->args[0], node->args) == -1) {
                perror("Ssh");
            }
            exit(EXIT_FAILURE);
      } else if (pid < 0) {
	    perror("Ssh: fork");
      } 
     
      else {
	    // --- PARENT PROCESS ---
            // Avoid setting process groups or touching terminal controls if in subshell
            if (!in_subshell) {
                setpgid(pid, pid);
            }

            if (!node->background && !in_subshell) {
		  // Foreground task: Only give it terminal control if STDIN is a tty
		  if (isatty(STDIN_FILENO)) {
			tcsetpgrp(STDIN_FILENO, pid);
		  }

		  // Wait for foreground job to finish OR stop (Ctrl+Z)
		  int status;
		  waitpid(pid, &status, WUNTRACED);
                
		  // Check if the child was suspended with Ctrl+Z
		  if (WIFSTOPPED(status)) {
			printf("\n[Process suspended: PID %d]\n", pid);
			add_job(pid, JOB_STOPPED, node->args[0]);
			if (isatty(STDIN_FILENO)) {
			      tcsetpgrp(STDIN_FILENO, getpgrp());
			}
			return 1;
		  }

		  // Reclaim control of terminal for the shell
		  if (isatty(STDIN_FILENO)) {
			tcsetpgrp(STDIN_FILENO, getpgrp());
		  }

		  // Return the actual exit status of the child
		  if (WIFEXITED(status)) {
			return WEXITSTATUS(status); 
		  } else if (WIFSIGNALED(status)) {
			return 1; 
		  }
	    } else {
		  if (in_subshell) {
			int status;
			waitpid(pid, &status, 0);
			if(WIFEXITED(status)) return WEXITSTATUS(status);
			return 1;
		  } else {
			// Background task (&): Register it in our job list
			add_job(pid, JOB_RUNNING, node->args[0]);
			printf("[%d] %d\n", get_job_id_by_pid(pid), pid);
			return 0;
		  }
	    }
      }

    }

    return 1;
}
