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
static char *HISTORY[HISTORY_LENGTH];
static int HISTORY_COUNT = 0;

// --- SIGNALS ---

void sigint_handler(int sig) {
  (void)sig;
  write(STDOUT_FILENO, "\n", 1);
}

void sigchld_handler(int sig) {
  (void)sig;
  int saved_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0);
  errno = saved_errno;
}

// --- UTILITIES ---

void refresh_cwd(void) {
  if (getcwd(CWD, sizeof(CWD)) == NULL) {
    perror("myShell: getcwd");
  }
}

void build_prompt(void) {
  snprintf(PROMPT, sizeof(PROMPT), "🪰  ");
}

int s_read(char *input, char **args, int max_args) {
  int i = 0;
  char *token = strtok(input, TOKEN_SEP);
  while (token != NULL && i < (max_args - 1)) {
    args[i++] = token;
    token = strtok(NULL, TOKEN_SEP);
  }
  args[i] = NULL;
  return i;
}

void history_push(const char *line) {
  if (line == NULL || *line == '\0') return;
  if (HISTORY_COUNT > 0 && strcmp(HISTORY[HISTORY_COUNT - 1], line) == 0) {
    return;
  }
  if (HISTORY_COUNT == HISTORY_LENGTH) {
    free(HISTORY[0]);
    memmove(HISTORY, HISTORY + 1, sizeof(char *) * (HISTORY_LENGTH - 1));
    HISTORY_COUNT--;
  }
  char *copy = strdup(line);
  if (copy == NULL) {
    perror("myShell: strdup");
    return;
  }
  HISTORY[HISTORY_COUNT++] = copy;
}

void history_free(void) {
  for (int i = 0; i < HISTORY_COUNT; i++) free(HISTORY[i]);
  HISTORY_COUNT = 0;
}

// --- BUILT-INS ---

typedef enum Builtin { CD, PWD, EXIT, HISTORY_CMD, INVALID } Builtin;

static char *expand_tilde(const char *path) {
  if (path == NULL) return NULL;
  if (path[0] != '~') return strdup(path);

  const char *home = getenv("HOME");
  if (home == NULL || *home == '\0') {
    return strdup(path); 
  }

  /* ~ alone */
  if (path[1] == '\0') return strdup(home);

  /* ~/rest */
  if (path[1] == '/') {
    size_t len = strlen(home) + strlen(path);
    char *out = malloc(len);
    if (out == NULL) return NULL;
    snprintf(out, len, "%s%s", home, path + 1);
    return out;
  }

  /* ~user is not supported; fall back to raw string */
  return strdup(path);
}

void builtin_impl_cd(char **args, size_t n_args) {
  const char *target;
  char *expanded = NULL;

  if (n_args < 1) {
    target = getenv("HOME");
    if (target == NULL || *target == '\0') {
      fprintf(stderr, "myShell: cd: HOME not set\n");
      return;
    }
  } else {
    expanded = expand_tilde(args[0]);
    if (expanded == NULL) {
      perror("myShell: cd");
      return;
    }
    target = expanded;
  }

  if (chdir(target) != 0) {
    fprintf(stderr, "myShell: cd: %s: %s\n", target, strerror(errno));
  }
  free(expanded);
  refresh_cwd();
}

void builtin_impl_pwd(char **args, size_t n_args) {
  (void)args; (void)n_args;
  fprintf(stdout, "%s\n", CWD);
}

void builtin_impl_exit(char **args, size_t n_args) {
  (void)args; (void)n_args;
  history_free();
  exit(0);
}

void builtin_impl_history(char **args, size_t n_args) {
  (void)args; (void)n_args;
  for (int i = 0; i < HISTORY_COUNT; i++) {
    fprintf(stdout, "%5d  %s\n", i + 1, HISTORY[i]);
  }
}

void (*BUILTIN_TABLE[])(char **args, size_t n_args) = {
  [CD] = builtin_impl_cd,
  [PWD] = builtin_impl_pwd,
  [EXIT] = builtin_impl_exit,
  [HISTORY_CMD] = builtin_impl_history,
};

Builtin builtin_code(char *cmd) {
  if (strcmp(cmd, "cd") == 0) return CD;
  if (strcmp(cmd, "pwd") == 0) return PWD;
  if (strcmp(cmd, "exit") == 0) return EXIT;
  if (strcmp(cmd, "history") == 0) return HISTORY_CMD;
  return INVALID;
}

// --- REDIRECTION & EXECUTION ---
int handle_redirection(char **args) {
  for (int i = 0; args[i] != NULL; i++) {
    int is_out = strcmp(args[i], ">") == 0;
    int is_in = strcmp(args[i], "<") == 0;
    if (!is_out && !is_in) continue;

    if (args[i + 1] == NULL) {
      fprintf(stderr, "myShell: syntax error near '%s'\n", args[i]);
      return -1;
    }

    int fd;
    if (is_out) {
      fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0) { perror("myShell: open"); return -1; }
      if (dup2(fd, STDOUT_FILENO) < 0) { perror("myShell: dup2"); close(fd); return -1; }
    } else {
      fd = open(args[i + 1], O_RDONLY);
      if (fd < 0) { perror("myShell: open"); return -1; }
      if (dup2(fd, STDIN_FILENO) < 0) { perror("myShell: dup2"); close(fd); return -1; }
    }
    close(fd);
    args[i] = NULL;
    args[i + 1] = NULL;
    break;
  }
  return 0;
}

int s_execute(char **cmd_args, int background) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("myShell: fork");
    return -1;
  }
  if (pid == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    if (handle_redirection(cmd_args) < 0) _exit(1);
    execvp(cmd_args[0], cmd_args);
    fprintf(stderr, "myShell: %s: %s\n", cmd_args[0], strerror(errno));
    _exit(127);
  }

  if (background) {
    fprintf(stdout, "[bg] pid=%d\n", (int)pid);
  } else {
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
    Builtin code = builtin_code(args[0]);
    if (code != INVALID) {
      BUILTIN_TABLE[code](args + 1, args_read - 1);
      return 0;
    }
    return s_execute(args, background);
  }

  args[pipe_idx] = NULL;
  char **cmd1 = args;
  char **cmd2 = &args[pipe_idx + 1];

  int fd[2];
  if (pipe(fd) < 0) {
    perror("myShell: pipe");
    return -1;
  }

  pid_t p1 = fork();
  if (p1 < 0) {
    perror("myShell: fork");
    close(fd[0]); close(fd[1]);
    return -1;
  }
  if (p1 == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    dup2(fd[1], STDOUT_FILENO);
    close(fd[0]); close(fd[1]);
    if (handle_redirection(cmd1) < 0) _exit(1);
    execvp(cmd1[0], cmd1);
    fprintf(stderr, "myShell: %s: %s\n", cmd1[0], strerror(errno));
    _exit(127);
  }

  pid_t p2 = fork();
  if (p2 < 0) {
    perror("myShell: fork");
    close(fd[0]); close(fd[1]);
    waitpid(p1, NULL, 0);
    return -1;
  }
  if (p2 == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    dup2(fd[0], STDIN_FILENO);
    close(fd[0]); close(fd[1]);
    if (handle_redirection(cmd2) < 0) _exit(1);
    execvp(cmd2[0], cmd2);
    fprintf(stderr, "myShell: %s: %s\n", cmd2[0], strerror(errno));
    _exit(127);
  }

  close(fd[0]); close(fd[1]);

  if (background) {
    fprintf(stdout, "[bg] pid=%d %d\n", (int)p1, (int)p2);
  } else {
    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
  }
  return 0;
}

// --- MAIN ---

int main(void) {
  refresh_cwd();
  build_prompt();

  struct sigaction sa_int = {0};
  sa_int.sa_handler = sigint_handler;
  sigemptyset(&sa_int.sa_mask);
  sa_int.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &sa_int, NULL) < 0) perror("myShell: sigaction SIGINT");

  struct sigaction sa_chld = {0};
  sa_chld.sa_handler = sigchld_handler;
  sigemptyset(&sa_chld.sa_mask);
  sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa_chld, NULL) < 0) perror("myShell: sigaction SIGCHLD");

  linenoiseHistorySetMaxLen(HISTORY_LENGTH);

  char *line;
  char *args[MAX_ARGS];
  while ((line = linenoise(PROMPT)) != NULL) {
    if (errno == EAGAIN) { errno = 0; linenoiseFree(line); continue; }

    char *line_for_history = strdup(line);

    int args_read = s_read(line, args, MAX_ARGS);
    if (args_read == 0) {
      free(line_for_history);
      linenoiseFree(line);
      continue;
    }

    int background = 0;
    if (args_read > 0 && strcmp(args[args_read - 1], "&") == 0) {
      background = 1;
      args[--args_read] = NULL;
    }

    if (args_read > 0) {
      s_execute_pipeline(args, args_read, background);
    }

    if (line_for_history != NULL) {
      linenoiseHistoryAdd(line_for_history);
      history_push(line_for_history);
      free(line_for_history);
    }
    linenoiseFree(line);
    build_prompt();
  }

  history_free();
  return 0;
}
