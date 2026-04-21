/**
 * @file myShell.c
 * @brief A Basic, lightweight custom shell implementation in C.
 * 
 * Features include dynamic prompt, command execution (foreground/background),
 * built-in commands (cd, pwd, exit, history), piping, I/O redirection,
 * and SIGINT handling.
 * 
 * To build: make ./myShell
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "linenoise.h"

#define HISTORY_LENGTH 1024 
#define MAX_ARGS 1024
#define TOKEN_SEP " \t"
#define PATH_MAX 4096
#define PROMPT_MAX 8192

char CWD[PATH_MAX];
char PROMPT[PROMPT_MAX];

/**
 * @brief Handles the SIGINT signal (Ctrl+C).
 * 
 * Ensures the shell does not terminate on SIGINT and instead provides a fresh line.
 * 
 * @param sig The signal number received.
 */
void sigint_handler(int sig) {
  (void)sig; // Explicitly ignore the unused signal parameter
  write(STDOUT_FILENO, "\n", 1);
}

/**
 * @brief Constructs a dynamic prompt string based on the current working directory.
 * 
 * Shortens the path by replacing the user's home directory with '~' if applicable.
 */
void build_prompt(void) {
  char display_path[PATH_MAX];
  char *home = getenv("HOME");

  // Substitute the home directory path with '~' for better readability
  if (home && strncmp(CWD, home, strlen(home)) == 0) {
    snprintf(display_path, sizeof(display_path), "~%s", CWD + strlen(home));
  } else {
    snprintf(display_path, sizeof(display_path), "%s", CWD);
  }
  
  // Format the final prompt string
  snprintf(PROMPT, sizeof(PROMPT), "🪰 %s $ ", display_path);
}


/**
 * @brief Tokenizes the input string into individual arguments.
 * 
 * @param input The raw input string from the user.
 * @param args An array of character pointers to store the parsed arguments.
 * @param max_args The maximum number of arguments the array can hold.
 * @return int The total number of arguments parsed.
 */
int s_read(char *input, char **args, int max_args){
  int i = 0;
  char *token = strtok(input, TOKEN_SEP);
  
  while(token != NULL && i < (max_args - 1)) {
    args[i++] = token;
    token = strtok(NULL, TOKEN_SEP);
  }
  args[i] = NULL; // Null-terminate the argument list for execvp
  return i;
}
/**
 * @brief Applies I/O redirection for the current process.
 *
 * Opens the specified files and redirects stdin/stdout accordingly.
 * Intended to be called from a child process before execvp().
 *
 * @param input_file  Path to a file for input redirection (NULL if none).
 * @param output_file Path to a file for output redirection (NULL if none).
 * @param append      If non-zero, output is appended instead of truncated.
 */
void apply_redirection(char *input_file, char *output_file, int append) {
  // Redirect stdin from a file
  if (input_file) {
    int fd_in = open(input_file, O_RDONLY);
    if (fd_in < 0) {
      perror("myShell: input redirection");
      exit(EXIT_FAILURE);
    }
    dup2(fd_in, STDIN_FILENO);
    close(fd_in);
  }

  // Redirect stdout to a file (truncate or append)
  if (output_file) {
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd_out = open(output_file, flags, 0644);
    if (fd_out < 0) {
      perror("myShell: output redirection");
      exit(EXIT_FAILURE);
    }
    dup2(fd_out, STDOUT_FILENO);
    close(fd_out);
  }
}

/**
 * @brief Parses and applies I/O redirection from a raw argument list.
 *
 * Scans the argument array for '<', '>', and '>>' operators, extracts
 * the associated filenames, nullifies them in-place, and applies the
 * redirection via apply_redirection(). Designed for use inside forked
 * child processes (e.g. pipeline stages).
 *
 * @param args  NULL-terminated argument array (modified in-place).
 * @return int  0 on success, -1 on syntax error.
 */
int handle_redirection(char **args) {
  char *input_file  = NULL;
  char *output_file = NULL;
  int append = 0;

  for (int i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], "<") == 0) {
      if (args[i + 1] == NULL) {
        fprintf(stderr, "myShell: syntax error near unexpected token '<'\n");
        return -1;
      }
      input_file = args[i + 1];
      args[i] = NULL;
      args[i + 1] = NULL;
      i++; // Skip the filename operand
    } else if (strcmp(args[i], ">>") == 0) {
      // Check '>>' before '>' to avoid partial match
      if (args[i + 1] == NULL) {
        fprintf(stderr, "myShell: syntax error near unexpected token '>>'\n");
        return -1;
      }
      output_file = args[i + 1];
      append = 1;
      args[i] = NULL;
      args[i + 1] = NULL;
      i++;
    } else if (strcmp(args[i], ">") == 0) {
      if (args[i + 1] == NULL) {
        fprintf(stderr, "myShell: syntax error near unexpected token '>'\n");
        return -1;
      }
      output_file = args[i + 1];
      append = 0;
      args[i] = NULL;
      args[i + 1] = NULL;
      i++;
    }
  }

  // Delegate actual file operations to the existing apply_redirection helper
  apply_redirection(input_file, output_file, append);
  return 0;
}

/**
 * @brief Executes a single command by forking a new process.
 *
 * Supports foreground/background execution and I/O redirection.
 *
 * @param cmd         The command to execute.
 * @param cmd_args    The argument list (argv-style, NULL-terminated).
 * @param background  If non-zero, the command runs asynchronously.
 * @param input_file  Path for input redirection (NULL if none).
 * @param output_file Path for output redirection (NULL if none).
 * @param append      If non-zero, output is appended instead of truncated.
 * @return int        The child's exit status, or -1 on failure.
 */
int s_execute(char *cmd,
              char **cmd_args,
              int background,
              char *input_file,
              char *output_file,
              int append) {
  int status = 0;
  pid_t pid = fork();

  if (pid < 0) {
    perror("myShell: fork");
    return -1;
  }

  if (pid == 0) {
    // Child: restore default signal handling
    signal(SIGINT, SIG_DFL);
    apply_redirection(input_file, output_file, append);

    // Execute the command using the path-searching version of exec
    if (execvp(cmd, cmd_args) == -1) {
      fprintf(stderr, "myShell: %s: command not found\n", cmd);
    }
    // Terminate child immediately if execution fails
    exit(EXIT_FAILURE);
  } else {
    // Parent: wait or report background PID
    if (!background) {
      if (waitpid(pid, &status, 0) != pid) {
        perror("myShell: waitpid");
      }
    } else {
      printf("[Process running in background with PID: %d]\n", pid);
    }
  }

  // Return the exit status if the child terminated normally
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}


/**
 * @brief Updates the global CWD variable with the current working directory.
 */
void refresh_cwd(void){
  if(getcwd(CWD, sizeof(CWD)) == NULL) {
    perror("myShell: getcwd");
  }
}

typedef enum Builtin {
  CD,
  PWD,
  EXIT,
  HISTORY,
  INVALID
} Builtin;

/**
 * @brief Implementation of the 'cd' built-in command.
 * 
 * Handles directory changes, including tilde (~) expansion.
 * 
 * @param args The arguments passed to 'cd'.
 * @param n_args The number of arguments.
 */
void builtin_impl_cd(char **args, size_t n_args) {
  if (n_args < 1) {
    fprintf(stderr, "cd: expected argument\n");
    return;
  }
  
  char *new_dir = args[0];
  char expanded_path[PATH_MAX];

  // Handle tilde (~) expansion to home directory
  if (new_dir[0] == '~') {
    char *home = getenv("HOME");
    if (home) {
      snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, new_dir + 1);
      new_dir = expanded_path;
    }
  }

  // Attempt to change directory
  if (chdir(new_dir) != 0) {
    fprintf(stderr, "myShell: cd: %s: %s\n", new_dir, strerror(errno));
    return;
  }

  refresh_cwd(); // Update CWD after successful directory change
}

/**
 * @brief Implementation of the 'pwd' built-in command.
 */
void builtin_impl_pwd(char **args, size_t n_args){
  fprintf(stdout, "%s\n", CWD);
}

/**
 * @brief Implementation of the 'exit' built-in command.
 */
void builtin_impl_exit(char **args, size_t n_args){
  printf("Goodbye! 🪰\n");
  exit(0);
}

/**
 * @brief Implementation of the 'history' built-in command.
 */
void builtin_impl_history(char **args, size_t n_args){
  // History functionality integration pending
}

void (*BUILTIN_TABLE[]) (char **args, size_t n_args) = {
  [CD] = builtin_impl_cd,
  [PWD] = builtin_impl_pwd,
  [EXIT] = builtin_impl_exit,
  [HISTORY] = builtin_impl_history
};

/**
 * @brief Maps a command string to its corresponding Builtin enumeration.
 * 
 * @param cmd The command name.
 * @return Builtin The command's enumeration value, or INVALID if not a built-in.
 */
Builtin builtin_code(char *cmd) {
  if (strcmp(cmd, "cd") == 0)       return CD;
  if (strcmp(cmd, "pwd") == 0)      return PWD;
  if (strcmp(cmd, "exit") == 0)     return EXIT;
  if (strcmp(cmd, "history") == 0)  return HISTORY;
  return INVALID;
}

/**
 * @brief Checks if a given command is a shell built-in.
 * 
 * @param cmd The command name.
 * @return int 1 if the command is a built-in, 0 otherwise.
 */
int is_builtin(char *cmd) {
  return builtin_code(cmd) != INVALID;
}

/**
 * @brief Executes a shell built-in command.
 * 
 * @param cmd The command name.
 * @param args Arguments for the command.
 * @param n_args Number of arguments.
 */
void s_execute_builtin(char *cmd, char **args, size_t n_args) {
  BUILTIN_TABLE[builtin_code(cmd)](args, n_args);
}

// --- PIPING ---

/**
 * @brief Executes a command line that may contain a single pipe ('|').
 *
 * Scans the argument list for a pipe token. If found, splits the command
 * into two stages, creates a pipe between them, and forks a child for each
 * stage. Each child handles its own I/O redirection independently.
 * If no pipe is present, falls through to built-in dispatch or s_execute().
 *
 * @param args       The parsed argument array (may be modified in-place).
 * @param args_read  Total number of tokens in the array.
 * @param background If non-zero, the pipeline runs asynchronously.
 * @return int       0 on success, -1 on failure.
 */
int s_execute_pipeline(char **args, int args_read, int background) {
  // Locate the first pipe operator in the argument list
  int pipe_idx = -1;
  for (int i = 0; i < args_read; i++) {
    if (args[i] && strcmp(args[i], "|") == 0) { pipe_idx = i; break; }
  }

  // --- Single-command fast path (no pipe detected) ---
  if (pipe_idx == -1) {
    Builtin code = builtin_code(args[0]);
    if (code != INVALID) {
      BUILTIN_TABLE[code](args + 1, (size_t)(args_read - 1));
      return 0;
    }

    // Parse redirection tokens before handing off to s_execute
    char *input_file  = NULL;
    char *output_file = NULL;
    int append = 0;

    for (int i = 1; i < args_read; i++) {
      if (args[i] == NULL) continue;
      if (strcmp(args[i], "<") == 0) {
        if (i + 1 < args_read && args[i + 1]) {
          input_file = args[i + 1];
          args[i] = NULL; args[i + 1] = NULL;
          i++;
        } else {
          fprintf(stderr, "myShell: syntax error near unexpected token '<'\n");
          return -1;
        }
      } else if (strcmp(args[i], ">>") == 0) {
        // Check '>>' before '>' to avoid partial match
        if (i + 1 < args_read && args[i + 1]) {
          output_file = args[i + 1]; append = 1;
          args[i] = NULL; args[i + 1] = NULL;
          i++;
        } else {
          fprintf(stderr, "myShell: syntax error near unexpected token '>>'\n");
          return -1;
        }
      } else if (strcmp(args[i], ">") == 0) {
        if (i + 1 < args_read && args[i + 1]) {
          output_file = args[i + 1]; append = 0;
          args[i] = NULL; args[i + 1] = NULL;
          i++;
        } else {
          fprintf(stderr, "myShell: syntax error near unexpected token '>'\n");
          return -1;
        }
      }
    }

    return s_execute(args[0], args, background, input_file, output_file, append);
  }

  // --- Pipeline path: split at the pipe token ---
  args[pipe_idx] = NULL;
  char **cmd1 = args;                   // Left-hand side of the pipe
  char **cmd2 = &args[pipe_idx + 1];    // Right-hand side of the pipe

  int fd[2];
  if (pipe(fd) < 0) {
    perror("myShell: pipe");
    return -1;
  }

  // Fork the producer (left side): stdout → pipe write end
  pid_t p1 = fork();
  if (p1 < 0) {
    perror("myShell: fork");
    close(fd[0]); close(fd[1]);
    return -1;
  }
  if (p1 == 0) {
    // Child: restore default signal disposition before exec
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    // Wire stdout to the pipe's write end
    dup2(fd[1], STDOUT_FILENO);
    close(fd[0]); close(fd[1]);

    // Apply any additional I/O redirection on this side
    if (handle_redirection(cmd1) < 0) _exit(1);

    execvp(cmd1[0], cmd1);
    fprintf(stderr, "myShell: %s: %s\n", cmd1[0], strerror(errno));
    _exit(127);
  }

  // Fork the consumer (right side): stdin ← pipe read end
  pid_t p2 = fork();
  if (p2 < 0) {
    perror("myShell: fork");
    close(fd[0]); close(fd[1]);
    waitpid(p1, NULL, 0); // Clean up the first child
    return -1;
  }
  if (p2 == 0) {
    // Child: restore default signal disposition before exec
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    // Wire stdin to the pipe's read end
    dup2(fd[0], STDIN_FILENO);
    close(fd[0]); close(fd[1]);

    // Apply any additional I/O redirection on this side
    if (handle_redirection(cmd2) < 0) _exit(1);

    execvp(cmd2[0], cmd2);
    fprintf(stderr, "myShell: %s: %s\n", cmd2[0], strerror(errno));
    _exit(127);
  }

  // Parent: close both ends of the pipe to avoid deadlock
  close(fd[0]); close(fd[1]);

  if (background) {
    fprintf(stdout, "[bg] pid=%d %d\n", (int)p1, (int)p2);
  } else {
    // Wait for both stages to finish before returning to the prompt
    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
  }
  return 0;
}

/**
 * @brief Main execution loop of the shell.
 */
int main(void) {
  // Fix for "dumb" terminals: linenoise skips its UTF-8 editor when
  // TERM is "dumb", which garbles our emoji prompt. Force a real one.
  if (!getenv("TERM") || strcmp(getenv("TERM"), "dumb") == 0) {
    setenv("TERM", "xterm-256color", 1);
  }

  refresh_cwd();
  build_prompt();

  // Register signal handler for SIGINT (Ctrl+C)
  signal(SIGINT, sigint_handler);

  printf("myShell v1.0 🪰\n");
  printf("Type 'exit' to quit.\n\n");

  if (!linenoiseHistorySetMaxLen(HISTORY_LENGTH)) {
    perror("myShell: linenoiseHistorySetMaxLen");
    exit(1);
  }

  char *line;
  char *args[MAX_ARGS];
  // Read-Evaluate-Print Loop
  while((line = linenoise(PROMPT)) != NULL){

    // Parse input into arguments
    int args_read = s_read(line, args, MAX_ARGS);

    // Ignore empty input
    if(args_read == 0){
      linenoiseFree(line);
      continue;
    }

    // --- Evaluation Step ---
    int background = 0;

    // Strip the background operator ('&') if it trails the command
    if (args_read > 0 && args[args_read - 1] &&
        strcmp(args[args_read - 1], "&") == 0) {
      background = 1;
      args[args_read - 1] = NULL;
      args_read--;
    }

    // Delegate to the pipeline handler (covers single commands and pipes)
    if (args_read > 0) {
      s_execute_pipeline(args, args_read, background);
    }

    // Post-execution cleanup and prompt refresh
    linenoiseHistoryAdd(line);
    linenoiseFree(line);
    build_prompt();
  }
  return 0;
}
