/**
 * @file myShell.c
 * @brief A professional, lightweight custom shell implementation in C.
 * 
 * Features include dynamic prompt, command execution (foreground/background),
 * built-in commands (cd, pwd, exit, history), and SIGINT handling.
 * 
 * To build: make ./myShell
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
 * @brief Executes a command by forking a new process.
 * 
 * Supports both synchronous (foreground) and asynchronous (background) execution.
 * 
 * @param cmd The command to execute.
 * @param cmd_args The arguments for the command.
 * @param background Flag indicating if the process should run in the background.
 * @return int The exit status of the command or -1 on failure.
 */
int s_execute(char *cmd, char **cmd_args, int background) {
  int status = 0;
  pid_t pid;

  pid = fork();
  if (pid < 0){
    perror("myShell: fork");
    return -1;
  }
  
  if (pid == 0){
    // Child Process: Reset signal handling and execute the command
    signal(SIGINT, SIG_DFL);
    if (execvp(cmd, cmd_args) == -1) {
      fprintf(stderr, "myShell: %s: command not found\n", cmd);
    }
    // Terminate child process immediately if execution fails
    exit(EXIT_FAILURE); 
  } else {
    // Parent Process
    if (!background) {
      // Synchronous execution: Wait for the child process to complete
      if (waitpid(pid, &status, 0) != pid){
        perror("myShell: waitpid");
      }
    } else {
      // Asynchronous execution: Do not wait for the child
      printf("[Process running in background with PID: %d]\n", pid);
    }
  }

  // Return the exit status if the child terminated normally
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
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


/**
 * @brief Main execution loop of the shell.
 */
int main(void) {
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

    // Evaluation Step
    char *cmd = args[0];
    char **cmd_args = args;

    // Detect background execution operator '&'
    int background = 0;
    if (args_read > 0 && strcmp(args[args_read-1], "&") == 0) {
      background = 1;
      args[args_read-1] = NULL; // Remove '&' from arguments before execution
    }
 
    // Route execution to either a built-in or an external command
    if (is_builtin(cmd)){
      s_execute_builtin(cmd, (cmd_args + 1), (size_t)(args_read - 1));
    } else {
      s_execute(cmd, cmd_args, background);
    }

    // Post-execution cleanup and prompt refresh
    linenoiseHistoryAdd(line);
    linenoiseFree(line);
    build_prompt();
  }
  return 0;
}