// make ./myShell

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include "linenoise.h"

#define HISTORY_LENGTH 1024 
#define MAX_ARGS 1024
#define TOKEN_SEP " \t"
#define PATH_MAX 4096
#define PROMPT_MAX 8192

char CWD[PATH_MAX];
char PROMPT[PROMPT_MAX];

// --- SIGNALS ---

void sigint_handler(int sig) {
  (void)sig;
  write(STDOUT_FILENO, "\n", 1);
}

void sigchld_handler(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

// --- UTILITIES ---

void refresh_cwd(void){
  if(getcwd(CWD, sizeof(CWD)) == NULL) {
    perror("myShell: getcwd");
  }
}

void build_prompt(void) {
  snprintf(PROMPT, sizeof(PROMPT), "🪰  ");
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

// --- BUILT-INS ---

typedef enum Builtin { CD, PWD, EXIT, HISTORY, INVALID } Builtin;

void builtin_impl_cd(char **args, size_t n_args) {
  if (n_args < 1) {
    return;
  }
  char *new_dir = args[0];
  if (chdir(new_dir) != 0) {
    fprintf(stderr, "myShell: cd: %s: %s\n", new_dir, strerror(errno));
  }
  refresh_cwd();
}

void builtin_impl_pwd(char **args, size_t n_args){ fprintf(stdout, "%s\n", CWD); }
void builtin_impl_exit(char **args, size_t n_args){ exit(0); }
void builtin_impl_history(char **args, size_t n_args){ /* Optional */ }

void (*BUILTIN_TABLE[]) (char **args, size_t n_args) = {
  [CD] = builtin_impl_cd, [PWD] = builtin_impl_pwd,
  [EXIT] = builtin_impl_exit, [HISTORY] = builtin_impl_history
};

Builtin builtin_code(char *cmd) {
  if (strcmp(cmd, "cd") == 0) return CD;
  if (strcmp(cmd, "pwd") == 0) return PWD;
  if (strcmp(cmd, "exit") == 0) return EXIT;
  if (strcmp(cmd, "history") == 0) return HISTORY;
  return INVALID;
}

// --- REDIRECTION & EXECUTION ---

void handle_redirection(char **args) {
  for (int i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], ">") == 0) {
      int fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
      args[i] = NULL;
    } else if (strcmp(args[i], "<") == 0) {
      int fd = open(args[i+1], O_RDONLY);
      if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); }
      args[i] = NULL;
    }
  }
}

int s_execute(char **cmd_args, int background) {
  pid_t pid = fork();
  if (pid == 0) {
    signal(SIGINT, SIG_DFL);
    handle_redirection(cmd_args);
    execvp(cmd_args[0], cmd_args);
    exit(1);
  } else if (pid > 0 && !background) {
    waitpid(pid, NULL, 0);
  }
  return 0;
}

// --- PIPING ---

int s_execute_pipeline(char **args, int args_read, int background) {
  int pipe_idx = -1;
  for (int i = 0; i < args_read; i++) {
    if (args[i] && strcmp(args[i], "|") == 0) { pipe_idx = i; break; }
  }

  if (pipe_idx == -1) {
    if (builtin_code(args[0]) != INVALID) {
      BUILTIN_TABLE[builtin_code(args[0])](args + 1, args_read - 1);
      return 0;
    }
    return s_execute(args, background);
  }

  args[pipe_idx] = NULL;
  char **cmd1 = args;
  char **cmd2 = &args[pipe_idx + 1];

  int fd[2];
  pipe(fd);

  if (fork() == 0) {
    signal(SIGINT, SIG_DFL);
    dup2(fd[1], STDOUT_FILENO);
    close(fd[0]); close(fd[1]);
    handle_redirection(cmd1);
    execvp(cmd1[0], cmd1);
    exit(1);
  }
  if (fork() == 0) {
    signal(SIGINT, SIG_DFL);
    dup2(fd[0], STDIN_FILENO);
    close(fd[0]); close(fd[1]);
    handle_redirection(cmd2);
    execvp(cmd2[0], cmd2);
    exit(1);
  }

  close(fd[0]); close(fd[1]);
  if (!background) { wait(NULL); wait(NULL); }
  return 0;
}

// --- MAIN ---

int main(void) {
  refresh_cwd();
  build_prompt();
  signal(SIGINT, sigint_handler);
  signal(SIGCHLD, sigchld_handler);

  linenoiseHistorySetMaxLen(HISTORY_LENGTH);

  char *line;
  char *args[MAX_ARGS];
  while((line = linenoise(PROMPT)) != NULL){
    if (errno == EAGAIN) { errno = 0; continue; }

    int args_read = s_read(line, args, MAX_ARGS);
    if(args_read == 0) { linenoiseFree(line); continue; }

    int background = 0;
    if (args_read > 0 && strcmp(args[args_read-1], "&") == 0) {
      background = 1;
      args[--args_read] = NULL;
    }

    s_execute_pipeline(args, args_read, background);

    linenoiseHistoryAdd(line);
    linenoiseFree(line);
    build_prompt(); 
  }
  return 0;
}
