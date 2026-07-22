#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include "arithmetic.h"
#include <glob.h>
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

static void expand_arithmetic_node(ASTNode *node) {
      for (int i = 0; i < node->arg_count; i++) {
	    char *expanded = expand_arithmetic(node->args[i]);

	    free(node->args[i]);
	    node->args[i] = expanded;
      }
}

int Ssh_num_builtins(void) {
      return sizeof(builtin_str) / sizeof(char *);
}

// Expansion Helper Function
// Scan an array of arguments, expand any that contain wildcards, and return a newly allocated, larger array
static char **expand_wildcards(char **args, int arg_count, int *new_count) {
      // Start with a reasonable capacity for the expanded arguments
      int capacity = arg_count + 8;
      char **expanded_args = malloc(sizeof(char *) * capacity);
      int count = 0;

      for (int i = 0; i < arg_count; i++) {
	    // Check for any wildcard pattern character (*, ?, or [])
            if (strpbrk(args[i], "*?[")) {
		  glob_t glob_result;
		  // GLOB_NOCHECK returns the pattern itself if no files match (like bash)
		  int ret = glob(args[i], GLOB_NOCHECK | GLOB_TILDE, NULL, &glob_result);
            
		  if (ret == 0) {
			// Resize expanded array if necessary to fit all matches
			while (count + (int)glob_result.gl_pathc >= capacity) {
			      capacity *= 2;
			      expanded_args = realloc(expanded_args, sizeof(char *) * capacity);
			}
                
			// Copy all matched paths into our new argument list
			for (size_t j = 0; j < glob_result.gl_pathc; j++) {
			      expanded_args[count++] = strdup(glob_result.gl_pathv[j]);
			}
			globfree(&glob_result);
			continue;
		  }
	    }
        
	    // If it's a regular argument or glob failed, just copy it over
	    if (count >= capacity - 1) {
		  capacity *= 2;
		  expanded_args = realloc(expanded_args, sizeof(char *) * capacity);
	    }
	    expanded_args[count++] = strdup(args[i]);
      }

      expanded_args[count] = NULL; // Null-terminate the new array
      *new_count = count;
      return expanded_args;
}
// Helper to evaluate a raw command string in a subshell
int execute_subshell_string(const char *cmd_str) {
      char *writable_cmd = strdup(cmd_str);
      if (!writable_cmd) return 1;

      int count = 0;
      LexToken *tokens = lex(writable_cmd, &count);
      int pos = 0;
      ASTNode *root = parse_sequence(tokens, &pos);
      int status = 0;

      if (root) {
	    status = execute_ast(root);
	    free_ast(root);
      }

      if (tokens) free(tokens);
      free(writable_cmd);
      return status;
}

// Applies file redirections specified in an AST node.
// Returns 0 on success, -1 on failure.
static int setup_redirections(ASTNode *node) {
      if (node->input_file) {
	    int fd_in = open(node->input_file, O_RDONLY);
	    if (fd_in < 0) {
		  perror("Ssh: open input file");
		  return -1;
	    }
	    dup2(fd_in, STDIN_FILENO);
	    close(fd_in);
      }

      if (node->output_file) {
	    int flags = O_WRONLY | O_CREAT;
	    if (node->append) {
		  flags |= O_APPEND;
	    } else {
		  flags |= O_TRUNC;
	    }

	    int fd_out = open(node->output_file, flags, 0644);
	    if (fd_out < 0) {
		  perror("Ssh: open output file");
		  return -1;
	    }
	    dup2(fd_out, STDOUT_FILENO);
	    close(fd_out);
      }

      return 0;
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

	    // Ignore interactive terminal signals inside command substitution
	    // so the subshell doesn't get suspended by background TTY writes
	    signal(SIGTTIN, SIG_IGN);
	    signal(SIGTTOU, SIG_IGN);

	    int status = execute_subshell_string(cmd_str);
	    exit(status); 
      } 
    
      // --- PARENT PROCESS ---
      close(pipefd[1]); // Parent doesn't write

      // Read the output from the pipe
      size_t capacity = BUFFER_SIZE;
      char *output = malloc(capacity);
      size_t total_bytes = 0;
      ssize_t bytes_read;

      while ((bytes_read = read(pipefd[0], output + total_bytes, capacity - total_bytes - 1)) > 0) {
	    // fprintf(stderr, "[DEBUG Parent] Read %ld bytes from subshell\n", (long)bytes_read);
	    total_bytes += bytes_read;
	    if (capacity - total_bytes < 128) {
		  capacity *= 2;
		  output = realloc(output, capacity);
	    }
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

		  // DEBUG
		  /*fprintf(stderr, "ARG = [%s]\n", node->args[i]);
		  fprintf(stderr, "INNER = [%s]\n", inner_cmd);
		  fprintf(stderr, "CAPTURED = [%s]\n", captured_output);
		  */

		  // Reconstruct the argument string with the substitution replaced
		  int prefix_len = start_idx;
		  int suffix_len = strlen(node->args[i]) - end_idx - 1;
		  int new_len = prefix_len + strlen(captured_output) + suffix_len;

		  char *new_arg = malloc(new_len + 1);
		  
		  // Copy prefix (before '$(')
		  strncpy(new_arg, node->args[i], prefix_len);
		  new_arg[prefix_len] = '\0';

		  // DEBUG		  
		  //fprintf(stderr, "NEW = [%s]\n", new_arg);
		  
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
		  if (strstr(new_arg, "$(")) i--; 
	      }
	  }
}

// Scans an argument array and evaluates environment variables ($VAR)
void expand_env_variables(ASTNode *node) {
      for (int i = 0; i < node->arg_count; i++) {
	    char *arg = node->args[i];
            char new_arg[BUFFER_SIZE] = {0};
            int new_len = 0;
            int len = strlen(arg);

            for (int j = 0; j < len; j++) {
		  // Skip $(...) command substitution so we don't destroy it
		  if (arg[j] == '$' && j + 1 < len && arg[j+1] == '(') {
			if (new_len < BUFFER_SIZE - 2) {
			      new_arg[new_len++] = '$';
			      new_arg[new_len++] = '(';
			}
			j++; // Skip '('
			 continue;
		  }	  
	    
                  // Catch environmental marker, make sure it is not part of $( command_sub )
                  if (arg[j] == '$' && j + 1 < len && arg[j+1] != '(') {
                        j++;
                        char var_name[256] = {0};
                        int v_idx = 0;

                        // Isolate alphanumeric variable key names
                        while (j < len && (isalnum((unsigned char)arg[j]) || arg[j] == '_')) {
                              if (v_idx < 255) {
                                    var_name[v_idx++] = arg[j];
                              }
                              j++;
                        }
                        j--; // Offset loop iteration jump

                        char *val = getenv(var_name);
                        if (val) {
                              size_t val_len = strlen(val);
                              if (new_len + val_len < BUFFER_SIZE - 1) {
                                    strcpy(new_arg + new_len, val);
                                    new_len += val_len;
                              }
                        }
                  } else {
                        if (new_len < BUFFER_SIZE - 1) {
                              new_arg[new_len++] = arg[j];
                        }
                  }
            }
            new_arg[new_len] = '\0';
            free(node->args[i]);
            node->args[i] = strdup(new_arg);
      }
}

int execute_ast(ASTNode *node) {
      if (!node) return 1;

      /*for (int d = 0; d < node->arg_count; d++) {
	    fprintf(stderr, "[DEBUG ARGS] args[%d] = '%s'\n", d, node->args[d]);
      }*/
      
      // Check for variable assignment (e.g., FOO=20)
      if (node->arg_count == 1 && strchr(node->args[0], '=') != NULL) {
	    char *eq = strchr(node->args[0], '=');
	    *eq = '\0';
	    char *name = node->args[0];
	    char *val = eq + 1;

	    setenv(name, val, 1);
	    return 0; // Success
      }

      // SUBSHELL ()
      if (node->type == NODE_SUBSHELL) {
	    pid_t pid = fork();
	    if (pid == 0) {
		  // --- CHILD PROCESS ---
		  in_subshell = 1;

		  if (setup_redirections(node) < 0) {
			exit(EXIT_FAILURE);
		  }
		  // Execute the inner sequence
		  int status = execute_ast(node->left);
		  exit(status);
	    } else if (pid < 0) {
		  perror("Ssh: fork subshell failed");
		  return 1;
	    } else {
		  // --- PARENT PROCESS ---
		  int status;
		  waitpid(pid, &status, 0);
		  if (WIFEXITED(status)) {
			return WEXITSTATUS(status);
		  }
		  return 1;
	    }
      }

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
		  in_subshell = 1;
		  dup2(pipefd[1], STDOUT_FILENO);
		  close(pipefd[0]); 
		  close(pipefd[1]);
		  execute_ast(node->left);
		  exit(EXIT_FAILURE);
	    }

	    pid_t pid2 = fork();
	    if (pid2 == 0) {
		  // Child 2 Read pipe
		  in_subshell = 1;
		  dup2(pipefd[0], STDIN_FILENO);
		  close(pipefd[0]); 
		  close(pipefd[1]);
		  int status = execute_ast(node->right);
		  exit(status);
	    }

	    //  Parent closes pipes and waits for childrens 
	    close(pipefd[0]); 
	    close(pipefd[1]);
      
	    int status1;
	    int status2;
	    waitpid(pid1, &status1, 0);
	    waitpid(pid2, &status2, 0);

	    // Return the actual exit status of the right-most command (pid2)
            if (WIFEXITED(status2)) {
                  return WEXITSTATUS(status2);
            } else if (WIFSIGNALED(status2)) {
                  return 128 + WTERMSIG(status2); // Standard shell exit code for signals
            }
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
	    return left_status;
      }

      // COMMAND
      if (node->type == NODE_COMMAND) {
	    if (node->arg_count == 0) return 0;

	    expand_arithmetic_node(node);
	    expand_command_substitution(node);
	    expand_env_variables(node);

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

	    if (strchr(node->args[0], '=') != NULL) {
	          char *arg = node->args[0];
	          char *eq = strchr(arg, '=');
	          *eq = '\0';
	          char *key = arg;
	          char *value = eq + 1;

	          // Strip wrapping quotes around assigned text values if present
	          size_t val_len = strlen(value);
	          if (val_len >= 2 && ((value[0] == '"' && value[val_len - 1] == '"') || 
	                               (value[0] == '\'' && value[val_len - 1] == '\''))) {
	                value[val_len - 1] = '\0';
	                value++;
	          }

	          if (setenv(key, value, 1) != 0) {
	                perror("Ssh: setenv");
	                return 1;
	          }
	          return 0; // Local assignment completed, skip spawning a child process
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

	    // Redirections
	    if (setup_redirections(node) < 0) {
		  exit(EXIT_FAILURE);
	    }

	    // Expand wildcards
	    int final_arg_count = 0;
            char **final_args = expand_wildcards(node->args, node->arg_count, &final_arg_count);

	    execvp(final_args[0], final_args);
              
            // If execvp returns, it failed
            perror(final_args[0]);
              
            // Clean up the temporary expanded array on failure
            for (int i = 0; i < final_arg_count; i++) free(final_args[i]);
            free(final_args);

	    //fprintf(stderr, "[DEBUG Grandchild] Executing: %s with in_subshell=%d\n", node->args[0], in_subshell);
            /*if (execvp(node->args[0], node->args) == -1) {
                perror("Ssh");
            }*/
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
