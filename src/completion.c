#include "common.h"
#include <readline/readline.h>
#include <readline/history.h>
#include "completion.h"

// Read directories and check file permissions
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

static char **matches_list = NULL;
static int matches_count = 0;
static int matches_index = 0;

static void free_matches_list(void) {
      if (matches_list) {
	    for (int i = 0; i < matches_count; i++) {
		  free(matches_list[i]);
	    }
	    free(matches_list);
	    matches_list = NULL;
      }

      matches_count = 0;
      matches_index = 0;
}

// Helper to add a unique match
static void add_match_unique(const char *name) {
      // Check for duplicates before adding
      for (int i = 0; i < matches_count; i++) {
	    if (strcmp(matches_list[i], name) == 0) {
		  return;
	    }
      }
    
      matches_list = realloc(matches_list, sizeof(char *) * (matches_count + 1));
      matches_list[matches_count] = strdup(name);
      matches_count++;
}

// Scans all folders inside the PATH environment variable for matching executables
static void scan_path_for_matches(const char *text, int len) {
      char *path_env = getenv("PATH");
      if (!path_env) return;

      // Duplicate PATH because strtok modifies the string
      char *path_dup = strdup(path_env);
      char *dir_path = strtok(path_dup, ":");

      while (dir_path != NULL) {
	    DIR *dir = opendir(dir_path);
	    if (dir) {
		  struct dirent *entry;
		  while ((entry = readdir(dir)) != NULL) {
			// Skip hidden files and directory links (. and ..)
			if (entry->d_name[0] == '.') continue;

			// Check if the filename starts with the typed text
			if (strncmp(entry->d_name, text, len) == 0) {
			      // Verify if it is an executable file
			      char full_path[PATH_MAX];
			      snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
                    
			      if (access(full_path, X_OK) == 0) {
				    add_match_unique(entry->d_name);
			      }
			}
		  }
		  closedir(dir);
	    }
	    dir_path = strtok(NULL, ":");
      }
      free(path_dup);
}

// Master generator that serves matches one-by-one to Readline
static char *command_generator(const char *text, int state) {
      if (!state) {
	    free_matches_list();
	    int len = strlen(text);

	    // Gather matching builtins
	    for (int i = 0; i < Ssh_num_builtins(); i++) {
		  if (strncmp(builtin_str[i], text, len) == 0) {
		  add_match_unique(builtin_str[i]);
		  }
	    }

	    // Gather matching system binaries in PATH
	    scan_path_for_matches(text, len);
      }

      // Feed matches to Readline one-by-one
      if (matches_index < matches_count) {
	    return strdup(matches_list[matches_index++]);
      }

      free_matches_list();
      return NULL;
}

static char **ssh_completion(const char *text, int start, int end) {
    (void)end;
    char **matches = NULL;

    // If the word is at the start of the line, complete commands (builtins + system binaries)
    if (start == 0) {
        matches = rl_completion_matches(text, command_generator);
    }
    
    // Otherwise, returning NULL allows standard file/directory completion to take over
    return matches;
}

void init_completion(void) {
    rl_attempted_completion_function = ssh_completion;
}
