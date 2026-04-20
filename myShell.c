// make ./myShell

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

// Handle Ctrl+C: print a newline so the prompt appears on a fresh line
void sigint_handler(int sig) {
  (void)sig;
  write(STDOUT_FILENO, "\n", 1);
}

// Build a dynamic prompt: 🪰 ~/path $ 
void build_prompt(void) {
  char display_path[PATH_MAX];
  char *home = getenv("HOME");

  // Shorten home directory to ~
  if (home && strncmp(CWD, home, strlen(home)) == 0) {
    snprintf(display_path, sizeof(display_path), "~%s", CWD + strlen(home));
  } else {
    snprintf(display_path, sizeof(display_path), "%s", CWD);
  }
  snprintf(PROMPT, sizeof(PROMPT), "🪰 %s $ ", display_path);
}


int s_read(char *input, char **args, int max_args){
  int i = 0;
  char *token = strtok(input, TOKEN_SEP);
  while(token != NULL && i < (max_args - 1)) {
    args[i++] = token;
    token = strtok(NULL, " \t");
  }
  args[i] = NULL;
  return i;
}

int s_execute(char *cmd, char **cmd_args, int background) {
  int status = 0;
  pid_t pid;

  pid = fork();
  if (pid < 0){
    perror("myShell: fork");
    return -1;
  }
  if (pid == 0){
    signal(SIGINT, SIG_DFL);  // Restore default signal handling for child
    if (execvp(cmd, cmd_args) == -1) {
      fprintf(stderr, "myShell: %s: command not found\n", cmd);
    }
    // If execvp fails, the child needs to die immediately.
    // Otherwise, it’ll loop back and start fighting the parent for terminal input.
    exit(EXIT_FAILURE); 
  } else {
    if (!background) {
      if (waitpid(pid, &status, 0) != pid){
        perror("myShell: waitpid");
      }
    } else {
      printf("[Process running in background with PID: %d]\n", pid);
    }
  }

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
}

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

// Fix: Prevent segfault by checking n_args before calling chdir(NULL).
void builtin_impl_cd(char **args, size_t n_args) {
  if (n_args < 1) {
    fprintf(stderr, "cd: expected argument\n");
    return;
  }
  char *new_dir = args[0];
  char expanded_path[PATH_MAX];

  if (new_dir[0] == '~') {
    char *home = getenv("HOME");
    if (home) {
      // Replace ~ with the home path, then append the rest of the string
      snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, new_dir + 1);
      new_dir = expanded_path;
    }
  }

  if (chdir(new_dir) != 0) {
    fprintf(stderr, "myShell: cd: %s: %s\n", new_dir, strerror(errno));
    return;
  }

  refresh_cwd();
}

void builtin_impl_pwd(char **args, size_t n_args){
  fprintf(stdout, "%s\n", CWD);
}
void builtin_impl_exit(char **args, size_t n_args){
  printf("Goodbye! 🪰\n");
  exit(0);
}
void builtin_impl_history(char **args, size_t n_args){

}

void (*BUILTIN_TABLE[]) (char **args, size_t n_args) = {
  [CD] = builtin_impl_cd,
  [PWD] = builtin_impl_pwd,
  [EXIT] = builtin_impl_exit,
  [HISTORY] = builtin_impl_history
};

Builtin builtin_code(char *cmd) {
  if (strcmp(cmd, "cd") == 0){
    return CD;
  } else if (strcmp(cmd, "pwd") == 0){
    return PWD;
  } else if (strcmp(cmd, "exit") == 0){
    return EXIT;
  } else if (strcmp(cmd, "history") == 0){
    return HISTORY;
  } else {
    return INVALID;
  }
}

int is_builtin(char *cmd) {
  return builtin_code(cmd) != INVALID;
}

void s_execute_builtin(char *cmd, char **args, size_t n_args) {
  BUILTIN_TABLE[builtin_code(cmd)](args, n_args);
}


int main(void) {
  refresh_cwd();
  build_prompt();

  signal(SIGINT, sigint_handler);  // Print newline on Ctrl+C

  printf("myShell v1.0 🪰\n");
  printf("Type 'exit' to quit.\n\n");

  if (!linenoiseHistorySetMaxLen(HISTORY_LENGTH)) {
    perror("myShell: linenoiseHistorySetMaxLen");
    exit(1);
  }

  char *line;
  char *args[MAX_ARGS];
  while((line = linenoise(PROMPT)) != NULL){

    // Read Step
    int args_read = s_read(line, args, MAX_ARGS);

    //Skip empty line
    if(args_read == 0){
      linenoiseFree(line);
      continue;
    }

    // Evaluation Step
    char *cmd = args[0];
    char **cmd_args = args;

    // Enables the parser to
    // identify special characters and clean the argument list before execution.
    int background = 0;
    if (args_read > 0 && strcmp(args[args_read-1], "&") == 0) {
      background = 1;
      args[args_read-1] = NULL; // Truncate the argument list 
    }
 
    if (is_builtin(cmd)){
      s_execute_builtin(cmd, (cmd_args+1), args_read-1);
    } else{
      s_execute(cmd, cmd_args, background);
    }

    linenoiseHistoryAdd(line);
    linenoiseFree(line);
    build_prompt();  // Refresh prompt after each command (CWD may have changed)
  }
  return 0;
}