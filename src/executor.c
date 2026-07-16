#include "executor.h"

const char *builtin_str[] = {
      "cd",
      "help",
      "exit"
      "jobs",
      "fg",
      "bg",
      "kills"
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
		  return 1;
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
		  }
		  return 1;
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
		  }
		  return 1;
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
		      }
		  } else {
		      fprintf(stderr, "Ssh: kill: no such job\n");
		  }
		  return 1;
	      }
      // --- EXTERNAL COMMAND EXECUTION & REDIRECTION ---
      pid_t pid = fork();
        if (pid == 0) {
            // --- CHILD PROCESS ---
            
            // Establish process group. 
            // If pgid is 0, the child uses its own PID as the process group leader.
            pid_t pgid = getpid();
            setpgid(0, pgid);
            
            // If it's a foreground job, claim terminal control
            if (!node->background) {
                tcsetpgrp(STDIN_FILENO, pgid);
            }

            // Restore default signal handlers so the child CAN be killed/suspended
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

            // Redirection logic (<, >, >>) ...

            if (execvp(node->args[0], node->args) == -1) {
                perror("Ssh");
            }
            exit(EXIT_FAILURE);
        } 
        else if (pid < 0) {
            perror("Ssh: fork");
        } 
        else {
            // --- PARENT PROCESS ---
            
            // Set process group in parent too (avoids a race condition)
            setpgid(pid, pid);

            if (!node->background) {
		  // Foreground task: Give it terminal control
		  tcsetpgrp(STDIN_FILENO, pid);
                
		  // Wait for foreground job to finish OR stop (Ctrl+Z)
		  int status;
		  waitpid(pid, &status, WUNTRACED);
                
		  // Check if the child was suspended with Ctrl+Z
		  if (WIFSTOPPED(status)) {
			printf("\n[Process suspended: PID %d]\n", pid);
			add_job(pid, JOB_STOPPED, node->args[0]);
		  }

		  // Reclaim control of terminal for the shell
		  tcsetpgrp(STDIN_FILENO, getpgrp());
	    } else {
		  // Background task (&): Register it in our job list
		  add_job(pid, JOB_RUNNING, node->args[0]);
		  printf("[%d] %d\n", get_job_id_by_pid(pid), pid);
	    }
      }

    }

    return 1;
}

